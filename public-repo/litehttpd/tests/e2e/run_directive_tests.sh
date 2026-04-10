#!/bin/bash
# =============================================================================
# Local Entry Script for OLS Directive E2E Tests
#
# Compiles the module, builds the Docker image, starts an OLS container,
# runs directive tests, and cleans up.
#
# Usage: ./tests/e2e/run_directive_tests.sh [--keep]
#   --keep  Keep the container running after tests for debugging
#
# Requirements: 3.1, 3.2, 3.3, 3.4, 22.5, 22.6
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
CONTAINER_NAME="ols-e2e"
IMAGE_NAME="ols-e2e-test"
OLS_PORT=8088
HEALTH_TIMEOUT=60
KEEP_CONTAINER=false

# Parse arguments
for arg in "$@"; do
    case "$arg" in
        --keep) KEEP_CONTAINER=true ;;
        *) echo "Unknown argument: $arg"; echo "Usage: $0 [--keep]"; exit 1 ;;
    esac
done

# Cleanup function
cleanup() {
    if [ "$KEEP_CONTAINER" = true ]; then
        echo ""
        echo "=== Container kept for debugging ==="
        echo "  Container: ${CONTAINER_NAME}"
        echo "  URL:       http://localhost:${OLS_PORT}/"
        echo "  Logs:      docker logs ${CONTAINER_NAME}"
        echo "  Shell:     docker exec -it ${CONTAINER_NAME} bash"
        echo "  Stop:      docker rm -f ${CONTAINER_NAME}"
        return
    fi
    echo ""
    echo "=== Cleaning up ==="
    docker rm -f "${CONTAINER_NAME}" 2>/dev/null || true
    echo "Container removed."
}

trap cleanup EXIT

echo "=== OLS Directive E2E Tests ==="
echo ""

# Step 1: Compile the module
echo "--- Step 1: Compiling module ---"
cd "${PROJECT_ROOT}"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "$(nproc 2>/dev/null || echo 4)"
echo "Module compiled successfully."
echo ""

# Step 2: Build Docker image
echo "--- Step 2: Building Docker image ---"
docker build -t "${IMAGE_NAME}" -f tests/e2e/Dockerfile .
echo "Docker image built successfully."
echo ""

# Step 3: Remove any existing container and start a new one
echo "--- Step 3: Starting OLS container ---"
docker rm -f "${CONTAINER_NAME}" 2>/dev/null || true
docker run -d --name "${CONTAINER_NAME}" -p "${OLS_PORT}:${OLS_PORT}" "${IMAGE_NAME}"
echo "Container started: ${CONTAINER_NAME}"
echo ""

# Step 4: Health check polling (max 60 seconds)
echo "--- Step 4: Health check (timeout: ${HEALTH_TIMEOUT}s) ---"
for i in $(seq 1 "${HEALTH_TIMEOUT}"); do
    if curl -sf "http://localhost:${OLS_PORT}/" > /dev/null 2>&1; then
        echo "OLS is ready (took ${i}s)."
        break
    fi
    if [ "$i" -eq "${HEALTH_TIMEOUT}" ]; then
        echo "ERROR: OLS failed to start within ${HEALTH_TIMEOUT} seconds."
        echo ""
        echo "=== Container logs ==="
        docker logs "${CONTAINER_NAME}" 2>&1 || true
        exit 1
    fi
    sleep 1
done
echo ""

# Step 5: Run directive tests
echo "--- Step 5: Running directive tests ---"
TEST_EXIT=0
bash "${SCRIPT_DIR}/test_directives.sh" || TEST_EXIT=$?
echo ""

# Step 6: Output test summary
if [ "$TEST_EXIT" -eq 0 ]; then
    echo "=== All directive tests passed ==="
else
    echo "=== Some directive tests failed (exit code: ${TEST_EXIT}) ==="
fi

exit "$TEST_EXIT"
