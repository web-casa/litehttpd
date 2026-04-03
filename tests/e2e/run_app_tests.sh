#!/bin/bash
# =============================================================================
# Local Entry Script for OLS PHP Application E2E Tests
#
# Compiles the module, starts the Docker Compose app stack (OLS + LSPHP +
# MariaDB), waits for services, runs selected PHP application tests, and
# cleans up.
#
# Usage: ./tests/e2e/run_app_tests.sh [--wordpress|--nextcloud|--drupal|--laravel|--joomla|--mediawiki|--all] [--keep]
#   --wordpress  Run WordPress tests only
#   --nextcloud  Run Nextcloud tests only
#   --drupal     Run Drupal tests only
#   --laravel    Run Laravel tests only
#   --joomla     Run Joomla tests only
#   --mediawiki  Run MediaWiki tests only
#   --all        Run all application tests
#   --keep       Keep containers running after tests for debugging
#
# Requirements: 22.1, 22.2, 22.3, 22.4, 22.6
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPOSE_FILE="${SCRIPT_DIR}/docker-compose.yml"
KEEP_CONTAINERS=false
APP_ARGS=()

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
for arg in "$@"; do
    case "$arg" in
        --keep)
            KEEP_CONTAINERS=true
            ;;
        --wordpress|--nextcloud|--drupal|--laravel|--joomla|--mediawiki|--all)
            APP_ARGS+=("$arg")
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 [--wordpress|--nextcloud|--drupal|--laravel|--joomla|--mediawiki|--all] [--keep]"
            exit 1
            ;;
    esac
done

if [[ ${#APP_ARGS[@]} -eq 0 ]]; then
    echo "Usage: $0 [--wordpress|--nextcloud|--drupal|--laravel|--joomla|--mediawiki|--all] [--keep]"
    echo "At least one application argument is required."
    exit 1
fi

# ---------------------------------------------------------------------------
# Cleanup function
# ---------------------------------------------------------------------------
cleanup() {
    if [ "$KEEP_CONTAINERS" = true ]; then
        echo ""
        echo "=== Containers kept for debugging ==="
        echo "  Compose file: ${COMPOSE_FILE}"
        echo "  OLS URL:      http://localhost:8088/"
        echo "  Logs:         docker compose -f ${COMPOSE_FILE} logs"
        echo "  Stop:         docker compose -f ${COMPOSE_FILE} down -v"
        return
    fi
    echo ""
    echo "=== Cleaning up ==="
    docker compose -f "${COMPOSE_FILE}" down -v 2>/dev/null || true
    echo "Containers and volumes removed."
}

trap cleanup EXIT

echo "========================================"
echo " OLS PHP Application E2E Tests (Local)"
echo "========================================"
echo ""

# ---------------------------------------------------------------------------
# Step 1: Compile the module
# ---------------------------------------------------------------------------
echo "--- Step 1: Compiling module ---"
cd "${PROJECT_ROOT}"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "$(nproc 2>/dev/null || echo 4)"
echo "Module compiled successfully."
echo ""

# ---------------------------------------------------------------------------
# Step 2: Start the app stack with docker compose
# ---------------------------------------------------------------------------
echo "--- Step 2: Starting app stack ---"
docker compose -f "${COMPOSE_FILE}" up -d --build
echo "App stack started."
echo ""

# ---------------------------------------------------------------------------
# Step 3: Wait for MariaDB and OLS health checks
# ---------------------------------------------------------------------------
echo "--- Step 3: Waiting for services ---"
bash "${SCRIPT_DIR}/lib/wait_for_services.sh"
echo ""

# ---------------------------------------------------------------------------
# Step 4: Run selected application tests
# ---------------------------------------------------------------------------
echo "--- Step 4: Running application tests ---"
TEST_EXIT=0
bash "${SCRIPT_DIR}/test_apps.sh" "${APP_ARGS[@]}" || TEST_EXIT=$?
echo ""

# ---------------------------------------------------------------------------
# Step 5: Output test summary
# ---------------------------------------------------------------------------
if [ "$TEST_EXIT" -eq 0 ]; then
    echo "========================================"
    echo " All application tests passed"
    echo "========================================"
else
    echo "========================================"
    echo " Some application tests failed (exit code: ${TEST_EXIT})"
    echo "========================================"
fi

exit "$TEST_EXIT"
