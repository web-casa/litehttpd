#!/bin/bash
# =============================================================================
# Integration Test Runner
#
# Starts OLS + MariaDB via Docker Compose, then runs verification scripts
# for each application.
#
# Usage:
#   ./run_all.sh                # Run all 4 apps
#   ./run_all.sh wordpress      # Run only WordPress
#   ./run_all.sh --build        # Rebuild images then run all
#   ./run_all.sh --build laravel  # Rebuild then run Laravel only
#   ./run_all.sh --down         # Tear down containers
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COMPOSE_FILE="${SCRIPT_DIR}/docker-compose.yml"

export OLS_HOST="${OLS_HOST:-http://localhost:8088}"
export OLS_CONTAINER="${OLS_CONTAINER:-ols-integ}"
export DB_CONTAINER="${DB_CONTAINER:-ols-integ-db}"

ALL_APPS=(wordpress laravel nextcloud drupal)
APPS_TO_RUN=()
DO_BUILD=false
TOTAL_PASS=0
TOTAL_FAIL=0

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
for arg in "$@"; do
    case "$arg" in
        --build)
            DO_BUILD=true
            ;;
        --down)
            echo ">>> Tearing down containers..."
            docker compose -f "$COMPOSE_FILE" down -v --remove-orphans
            echo ">>> Done."
            exit 0
            ;;
        wordpress|laravel|nextcloud|drupal)
            APPS_TO_RUN+=("$arg")
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 [--build] [--down] [wordpress|laravel|nextcloud|drupal]"
            exit 1
            ;;
    esac
done

# Default: all apps
if [[ ${#APPS_TO_RUN[@]} -eq 0 ]]; then
    APPS_TO_RUN=("${ALL_APPS[@]}")
fi

# ---------------------------------------------------------------------------
# Ensure module is built
# ---------------------------------------------------------------------------
if [[ ! -f "${SCRIPT_DIR}/../build/litehttpd_htaccess.so" ]]; then
    echo ">>> litehttpd_htaccess.so not found, building..."
    cmake -B "${SCRIPT_DIR}/../build" -DCMAKE_BUILD_TYPE=Release "${SCRIPT_DIR}/.."
    cmake --build "${SCRIPT_DIR}/../build" -j"$(nproc)"
fi

# ---------------------------------------------------------------------------
# Start services
# ---------------------------------------------------------------------------
echo "========================================"
echo " LiteHTTPD Integration Tests"
echo "========================================"
echo " Apps: ${APPS_TO_RUN[*]}"
echo ""

if $DO_BUILD; then
    echo ">>> Building images..."
    docker compose -f "$COMPOSE_FILE" build --no-cache
fi

echo ">>> Starting services..."
docker compose -f "$COMPOSE_FILE" up -d

# Initialize volume-mounted docroot (Dockerfile files are hidden by named volume)
docker exec "${OLS_CONTAINER}" bash -c '
    mkdir -p /var/www/vhosts/localhost/html
    echo "<h1>Integration Test Stack OK</h1>" > /var/www/vhosts/localhost/html/index.html
    chown -R nobody:nogroup /var/www/vhosts/localhost/html 2>/dev/null || true
' 2>/dev/null || true

# Source assert lib for wait functions
source "${SCRIPT_DIR}/lib/assert.sh"
wait_for_db 60
wait_for_ols 60

# ---------------------------------------------------------------------------
# Run tests for each app
# ---------------------------------------------------------------------------
EXIT_CODE=0

for app in "${APPS_TO_RUN[@]}"; do
    VERIFY_SCRIPT="${SCRIPT_DIR}/apps/${app}/verify.sh"

    if [[ ! -f "$VERIFY_SCRIPT" ]]; then
        echo "WARNING: No verify.sh found for ${app}, skipping."
        continue
    fi

    echo ""
    echo ">>> Running ${app} tests..."
    if bash "$VERIFY_SCRIPT"; then
        echo ">>> ${app}: ALL PASSED"
    else
        echo ">>> ${app}: SOME TESTS FAILED"
        EXIT_CODE=1
    fi
done

# ---------------------------------------------------------------------------
# Final summary
# ---------------------------------------------------------------------------
echo ""
echo "========================================"
echo " Integration Test Suite Complete"
echo "========================================"
echo " Apps tested: ${APPS_TO_RUN[*]}"

if [[ "$EXIT_CODE" -eq 0 ]]; then
    echo " Result: ALL PASSED"
else
    echo " Result: SOME FAILURES — check output above"
fi
echo "========================================"
echo ""
echo "To tear down: $0 --down"

exit "$EXIT_CODE"
