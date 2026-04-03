#!/bin/bash
# =============================================================================
# E2E Test Assertion Library for OLS .htaccess Module
#
# Provides assertion functions, test lifecycle helpers, and summary reporting
# for curl-based HTTP testing against an OLS container.
#
# Requirements: 9.1, 9.2, 9.3
# =============================================================================

set -euo pipefail

# ---------------------------------------------------------------------------
# Global variables
# ---------------------------------------------------------------------------
OLS_HOST="${OLS_HOST:-http://localhost:8088}"
OLS_DOCROOT="${OLS_DOCROOT:-/var/www/vhosts/localhost/html}"
OLS_CONTAINER="${OLS_CONTAINER:-ols-e2e}"
PASS_COUNT=0
FAIL_COUNT=0

# Internal: store last curl response for diagnostics on failure
_LAST_RESPONSE_HEADERS=""
_LAST_RESPONSE_BODY=""
_LAST_STATUS_CODE=""

# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

# Fetch a URL and cache the full response (headers + body + status code).
# Usage: _fetch <method> <path> [extra_curl_args...]
_fetch() {
    local method="$1"
    local path="$2"
    shift 2

    local url="${OLS_HOST}${path}"
    local tmp_headers
    tmp_headers=$(mktemp)

    _LAST_RESPONSE_BODY=$(curl -s -X "$method" \
        --max-time 10 \
        -D "$tmp_headers" \
        -o - \
        "$@" \
        "$url") || true

    _LAST_RESPONSE_HEADERS=$(cat "$tmp_headers")
    rm -f "$tmp_headers"

    # Extract status code from the first HTTP status line
    _LAST_STATUS_CODE=$(echo "$_LAST_RESPONSE_HEADERS" | head -1 | grep -oP '\d{3}' | head -1)
}

# Print failure diagnostics — actual HTTP response content (Req 9.2)
_print_failure() {
    local assertion_name="$1"
    local detail="$2"
    echo "  FAIL: ${assertion_name} — ${detail}"
    echo "  --- Actual HTTP Response ---"
    echo "  Status: ${_LAST_STATUS_CODE}"
    echo "  Headers:"
    echo "$_LAST_RESPONSE_HEADERS" | sed 's/^/    /'
    echo "  Body (first 500 chars):"
    echo "${_LAST_RESPONSE_BODY:0:500}" | sed 's/^/    /'
    echo "  ---"
}

# ---------------------------------------------------------------------------
# Core assertion functions
# ---------------------------------------------------------------------------

# assert_status_code — Verify HTTP status code
# Usage: assert_status_code <method> <path> <expected_code>
assert_status_code() {
    local method="$1"
    local path="$2"
    local expected="$3"

    _fetch "$method" "$path"

    if [[ "$_LAST_STATUS_CODE" == "$expected" ]]; then
        return 0
    else
        _print_failure "assert_status_code" \
            "Expected status ${expected}, got ${_LAST_STATUS_CODE} (${method} ${path})"
        return 1
    fi
}

# assert_header_exists — Verify a response header is present
# Usage: assert_header_exists <method> <path> <header_name>
assert_header_exists() {
    local method="$1"
    local path="$2"
    local header_name="$3"

    _fetch "$method" "$path"

    if grep -qi "^${header_name}:" <<< "$_LAST_RESPONSE_HEADERS" 2>/dev/null; then
        return 0
    else
        _print_failure "assert_header_exists" \
            "Header '${header_name}' not found in response (${method} ${path})"
        return 1
    fi
}

# assert_header_value — Verify a response header has a specific value
# Usage: assert_header_value <method> <path> <header_name> <expected_value>
assert_header_value() {
    local method="$1"
    local path="$2"
    local header_name="$3"
    local expected_value="$4"

    _fetch "$method" "$path"

    local actual_value
    actual_value=$(echo "$_LAST_RESPONSE_HEADERS" \
        | grep -i "^${header_name}:" \
        | head -1 \
        | sed "s/^[^:]*: *//" \
        | tr -d '\r')

    if [[ "$actual_value" == *"$expected_value"* ]]; then
        return 0
    else
        _print_failure "assert_header_value" \
            "Header '${header_name}': expected '${expected_value}', got '${actual_value}' (${method} ${path})"
        return 1
    fi
}

# assert_header_absent — Verify a response header is NOT present
# Usage: assert_header_absent <method> <path> <header_name>
assert_header_absent() {
    local method="$1"
    local path="$2"
    local header_name="$3"

    _fetch "$method" "$path"

    if grep -qi "^${header_name}:" <<< "$_LAST_RESPONSE_HEADERS" 2>/dev/null; then
        _print_failure "assert_header_absent" \
            "Header '${header_name}' should be absent but was found (${method} ${path})"
        return 1
    else
        return 0
    fi
}

# assert_body_contains — Verify response body contains expected text
# Usage: assert_body_contains <method> <path> <expected_text>
assert_body_contains() {
    local method="$1"
    local path="$2"
    local expected_text="$3"

    _fetch "$method" "$path"

    if grep -qF "$expected_text" <<< "$_LAST_RESPONSE_BODY" 2>/dev/null; then
        return 0
    else
        _print_failure "assert_body_contains" \
            "Body does not contain '${expected_text}' (${method} ${path})"
        return 1
    fi
}

# assert_redirect — Verify redirect (status code + Location header)
# Usage: assert_redirect <method> <path> <expected_status> <expected_location>
assert_redirect() {
    local method="$1"
    local path="$2"
    local expected_status="$3"
    local expected_location="$4"

    # Use -L to NOT follow redirects (we want to inspect the redirect itself)
    local url="${OLS_HOST}${path}"
    local tmp_headers
    tmp_headers=$(mktemp)

    _LAST_RESPONSE_BODY=$(curl -s -X "$method" \
        --max-time 10 \
        -D "$tmp_headers" \
        -o - \
        "$url") || true

    _LAST_RESPONSE_HEADERS=$(cat "$tmp_headers")
    rm -f "$tmp_headers"
    _LAST_STATUS_CODE=$(echo "$_LAST_RESPONSE_HEADERS" | head -1 | grep -oP '\d{3}' | head -1)

    local ok=true

    if [[ "$_LAST_STATUS_CODE" != "$expected_status" ]]; then
        _print_failure "assert_redirect (status)" \
            "Expected status ${expected_status}, got ${_LAST_STATUS_CODE} (${method} ${path})"
        ok=false
    fi

    local actual_location
    actual_location=$(echo "$_LAST_RESPONSE_HEADERS" \
        | grep -i "^location:" \
        | head -1 \
        | sed "s/^[^:]*: *//" \
        | tr -d '\r')

    if [[ "$actual_location" != *"$expected_location"* ]]; then
        _print_failure "assert_redirect (location)" \
            "Expected Location containing '${expected_location}', got '${actual_location}' (${method} ${path})"
        ok=false
    fi

    if $ok; then
        return 0
    else
        return 1
    fi
}

# ---------------------------------------------------------------------------
# Test lifecycle helpers
# ---------------------------------------------------------------------------

# deploy_htaccess — Deploy .htaccess content to the container document root
# Usage: deploy_htaccess <htaccess_content> [subpath]
#   subpath: optional subdirectory relative to docroot (default: root)
deploy_htaccess() {
    local content="$1"
    local subpath="${2:-}"
    local target_dir="${OLS_DOCROOT}"

    if [[ -n "$subpath" ]]; then
        target_dir="${OLS_DOCROOT}/${subpath}"
    fi

    local tmp_file
    tmp_file=$(mktemp)
    echo "$content" > "$tmp_file"

    docker cp "$tmp_file" "${OLS_CONTAINER}:${target_dir}/.htaccess"
    rm -f "$tmp_file"

    # Brief pause to let OLS pick up the new .htaccess
    sleep 0.3
}

# cleanup_htaccess — Remove .htaccess from the container document root
# Usage: cleanup_htaccess [subpath]
cleanup_htaccess() {
    local subpath="${1:-}"
    local target_dir="${OLS_DOCROOT}"

    if [[ -n "$subpath" ]]; then
        target_dir="${OLS_DOCROOT}/${subpath}"
    fi

    docker exec "${OLS_CONTAINER}" rm -f "${target_dir}/.htaccess" 2>/dev/null || true

    # Brief pause to let OLS notice the removal
    sleep 0.2
}

# run_test — Execute a single test case (name + assertion function)
# Usage: run_test <test_name> <test_function>
#
# The test function should return 0 on success, non-zero on failure.
# Each test independently tracks pass/fail via global counters (Req 9.1).
run_test() {
    local test_name="$1"
    local test_fn="$2"

    echo -n "TEST: ${test_name} ... "

    if $test_fn; then
        echo "PASS"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo "FAIL"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

# print_summary — Output test summary (pass/fail/total) (Req 9.3)
# Usage: print_summary
# Returns non-zero exit code if any test failed.
print_summary() {
    local total=$((PASS_COUNT + FAIL_COUNT))
    echo ""
    echo "========================================"
    echo " Test Summary"
    echo "========================================"
    echo " Total:  ${total}"
    echo " Passed: ${PASS_COUNT}"
    echo " Failed: ${FAIL_COUNT}"
    echo "========================================"

    if [[ "$FAIL_COUNT" -gt 0 ]]; then
        echo " RESULT: FAILED"
        return 1
    else
        echo " RESULT: ALL PASSED"
        return 0
    fi
}
