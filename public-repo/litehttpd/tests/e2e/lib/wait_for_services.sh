#!/bin/bash
# =============================================================================
# Service Health Check Wait Script
#
# Waits for MariaDB and OLS to be fully ready before running tests.
# Used by docker-compose app testing workflow.
#
# Requirements: 3.1, 3.2, 13.1
# =============================================================================

set -euo pipefail

DB_CONTAINER="${DB_CONTAINER:-ols-app-db}"
OLS_HOST="${OLS_HOST:-http://localhost:8088}"
MAX_WAIT="${MAX_WAIT:-60}"

# ---------------------------------------------------------------------------
# Wait for MariaDB to accept connections (max 60s)
# ---------------------------------------------------------------------------
wait_for_db() {
    echo "Waiting for MariaDB to be ready (max ${MAX_WAIT}s)..."
    local elapsed=0

    while [ "$elapsed" -lt "$MAX_WAIT" ]; do
        if docker exec "$DB_CONTAINER" mariadb -uroot -prootpass -e "SELECT 1" >/dev/null 2>&1; then
            echo "MariaDB is ready (${elapsed}s)."
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done

    echo "ERROR: MariaDB did not become ready within ${MAX_WAIT}s."
    echo "--- MariaDB container logs ---"
    docker logs "$DB_CONTAINER" 2>&1 | tail -50 || true
    return 1
}

# ---------------------------------------------------------------------------
# Wait for OLS to respond to HTTP requests (max 60s)
# ---------------------------------------------------------------------------
wait_for_ols() {
    echo "Waiting for OLS to respond at ${OLS_HOST} (max ${MAX_WAIT}s)..."
    local elapsed=0

    while [ "$elapsed" -lt "$MAX_WAIT" ]; do
        if curl -sf --max-time 3 "${OLS_HOST}/" >/dev/null 2>&1; then
            echo "OLS is ready (${elapsed}s)."
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done

    echo "ERROR: OLS did not respond within ${MAX_WAIT}s."
    echo "--- OLS container logs ---"
    local ols_container="${OLS_CONTAINER:-ols-app-e2e}"
    docker logs "$ols_container" 2>&1 | tail -50 || true
    docker exec "$ols_container" cat /usr/local/lsws/logs/error.log 2>&1 | tail -30 || true
    return 1
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
echo "========================================"
echo " Waiting for services to be ready..."
echo "========================================"

wait_for_db
wait_for_ols

echo "========================================"
echo " All services are ready."
echo "========================================"
