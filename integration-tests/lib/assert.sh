#!/bin/bash
# =============================================================================
# Integration Test Assertion Library
#
# Provides HTTP assertion functions for verifying .htaccess behavior
# against a running OLS instance with real PHP applications.
# =============================================================================

set -euo pipefail

OLS_HOST="${OLS_HOST:-http://localhost:8088}"
OLS_CONTAINER="${OLS_CONTAINER:-ols-integ}"
PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0

_LAST_RESPONSE_HEADERS=""
_LAST_RESPONSE_BODY=""
_LAST_STATUS_CODE=""

# ---------------------------------------------------------------------------
# Internal: fetch URL and cache response
# ---------------------------------------------------------------------------
_fetch() {
    local method="$1" path="$2"
    shift 2
    local url="${OLS_HOST}${path}"
    local tmp_headers
    tmp_headers=$(mktemp)

    local curl_err
    _LAST_RESPONSE_BODY=$(curl -s -X "$method" \
        --max-time 10 \
        -D "$tmp_headers" \
        -o - \
        "$@" \
        "$url" 2>/dev/null) || curl_err=$?

    _LAST_RESPONSE_HEADERS=$(cat "$tmp_headers" 2>/dev/null)
    rm -f "$tmp_headers"

    if [[ -n "${curl_err:-}" ]]; then
        _LAST_STATUS_CODE=""
        return
    fi
    _LAST_STATUS_CODE=$(echo "$_LAST_RESPONSE_HEADERS" | head -1 | grep -oP 'HTTP/[^ ]+ \K\d{3}' | head -1)
}

_print_failure() {
    local name="$1" detail="$2"
    echo "  FAIL: ${name} — ${detail}"
    echo "  Status: ${_LAST_STATUS_CODE}"
    echo "  Headers (first 10):"
    echo "$_LAST_RESPONSE_HEADERS" | head -10 | sed 's/^/    /'
    echo "  Body (first 300 chars):"
    while IFS= read -r line; do
        printf '    %s\n' "$line"
    done <<< "${_LAST_RESPONSE_BODY:0:300}"
}

# ---------------------------------------------------------------------------
# Assertion functions
# ---------------------------------------------------------------------------

assert_http_status() {
    local path="$1" expected="$2"
    _fetch GET "$path"
    if [[ -z "$_LAST_STATUS_CODE" ]]; then
        echo "ERROR: no response from ${OLS_HOST}${path}" >&2
        return 1
    fi
    [[ "$_LAST_STATUS_CODE" == "$expected" ]]
}

assert_body_contains() {
    local path="$1" text="$2"
    _fetch GET "$path"
    grep -qF "$text" <<< "$_LAST_RESPONSE_BODY" 2>/dev/null
}

assert_header() {
    local path="$1" header_name="$2" expected_value="$3"
    _fetch GET "$path"
    local actual
    actual=$(echo "$_LAST_RESPONSE_HEADERS" \
        | grep -i "^${header_name}:" \
        | head -1 \
        | sed "s/^[^:]*: *//" \
        | tr -d '\r')
    [[ "$actual" == *"$expected_value"* ]]
}

assert_header_absent() {
    local path="$1" header_name="$2"
    _fetch GET "$path"
    ! grep -qi "^${header_name}:" <<< "$_LAST_RESPONSE_HEADERS" 2>/dev/null
}

assert_redirect() {
    local path="$1" expected_status="$2" expected_location="$3"
    _fetch GET "$path"
    [[ "$_LAST_STATUS_CODE" == "$expected_status" ]] || return 1
    local location
    location=$(echo "$_LAST_RESPONSE_HEADERS" \
        | grep -i "^location:" | head -1 \
        | sed "s/^[^:]*: *//" | tr -d '\r')
    [[ "$location" == *"$expected_location"* ]]
}

# ---------------------------------------------------------------------------
# Test runner
# ---------------------------------------------------------------------------

run_test() {
    local test_name="$1" test_fn="$2"
    echo -n "  ${test_name} ... "
    if $test_fn; then
        echo "PASS"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo "FAIL"
        _print_failure "${test_name}" "assertion failed"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

skip_test() {
    local test_name="$1" reason="$2"
    echo "  ${test_name} ... SKIP (${reason})"
    SKIP_COUNT=$((SKIP_COUNT + 1))
}

print_summary() {
    local app="${1:-all}"
    local total=$((PASS_COUNT + FAIL_COUNT + SKIP_COUNT))
    echo ""
    echo "========================================"
    echo " ${app} — Test Summary"
    echo "========================================"
    echo " Total:   ${total}"
    echo " Passed:  ${PASS_COUNT}"
    echo " Failed:  ${FAIL_COUNT}"
    echo " Skipped: ${SKIP_COUNT}"
    echo "========================================"
    [[ "$FAIL_COUNT" -eq 0 ]]
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

deploy_htaccess() {
    local content="$1" subpath="${2:-}"
    local target="${OLS_DOCROOT:-/var/www/vhosts/localhost/html}"
    [[ -n "$subpath" ]] && target="${target}/${subpath}"
    local tmp
    tmp=$(mktemp)
    echo "$content" > "$tmp"
    docker cp "$tmp" "${OLS_CONTAINER}:${target}/.htaccess"
    rm -f "$tmp"
    sleep 0.3
}

wait_for_ols() {
    local max_wait="${1:-60}" elapsed=0
    echo "Waiting for OLS at ${OLS_HOST} ..."
    while [ "$elapsed" -lt "$max_wait" ]; do
        if curl -sf --max-time 3 "${OLS_HOST}/" >/dev/null 2>&1; then
            echo "OLS ready (${elapsed}s)."
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    echo "ERROR: OLS not ready within ${max_wait}s"
    return 1
}

wait_for_db() {
    local db_container="${DB_CONTAINER:-ols-integ-db}" max_wait="${1:-60}" elapsed=0
    echo "Waiting for MariaDB ..."
    while [ "$elapsed" -lt "$max_wait" ]; do
        if docker exec "$db_container" mariadb -uroot -prootpass -e "SELECT 1" >/dev/null 2>&1; then
            echo "MariaDB ready (${elapsed}s)."
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    echo "ERROR: MariaDB not ready within ${max_wait}s"
    return 1
}

docker_exec() {
    docker exec "${OLS_CONTAINER}" "$@"
}
