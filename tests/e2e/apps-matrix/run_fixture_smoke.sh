#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# shellcheck source=tests/e2e/apps-matrix/lib/matrix_common.sh
source "${SCRIPT_DIR}/lib/matrix_common.sh"

BASE_DIR="${MATRIX_SMOKE_BASE_DIR:-$(mktemp -d /tmp/apps-matrix-smoke.XXXXXX)}"
KEEP_OUT="${MATRIX_KEEP_SMOKE_OUT:-0}"
SERVER_PIDS=()

cleanup() {
    local pid
    for pid in "${SERVER_PIDS[@]:-}"; do
        kill "${pid}" >/dev/null 2>&1 || true
        wait "${pid}" >/dev/null 2>&1 || true
    done
    if [[ "${KEEP_OUT}" != "1" ]]; then
        rm -rf "${BASE_DIR}"
    else
        matrix_warn "keeping smoke output at ${BASE_DIR}"
    fi
}
trap cleanup EXIT

find_free_port() {
    python3 - <<'PY'
import socket
with socket.socket() as sock:
    sock.bind(("127.0.0.1", 0))
    print(sock.getsockname()[1])
PY
}

wait_for_url() {
    local url="$1"
    for _ in $(seq 1 40); do
        if curl -fsS "${url}" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

start_fixture_server() {
    local docroot="$1"
    local port="$2"
    python3 "${SCRIPT_DIR}/lib/fixture_server.py" \
        --docroot "${docroot}" \
        --host 127.0.0.1 \
        --port "${port}" &
    SERVER_PIDS+=("$!")
}

stop_fixture_servers() {
    local pid
    for pid in "${SERVER_PIDS[@]:-}"; do
        kill "${pid}" >/dev/null 2>&1 || true
        wait "${pid}" >/dev/null 2>&1 || true
    done
    SERVER_PIDS=()
}

run_smoke_scenario() {
    local scenario="$1"
    shift
    local out_dir="${BASE_DIR}/${scenario}"
    local fixture_root="${out_dir}/fixtures/docroots"
    local apache_port ols_module_port
    local args=()
    local case_id

    for case_id in "$@"; do
        args+=(--case "${case_id}")
    done

    matrix_banner "Fixture smoke ${scenario}"
    MATRIX_OUT_DIR="${out_dir}" \
    MATRIX_FIXTURE_ROOT="${fixture_root}" \
    MATRIX_ENGINES="apache,ols_module" \
        bash "${SCRIPT_DIR}/run_matrix.sh" --install-only --scenario "${scenario}"

    apache_port="$(find_free_port)"
    ols_module_port="$(find_free_port)"
    start_fixture_server "${fixture_root}/apache/${scenario}" "${apache_port}"
    start_fixture_server "${fixture_root}/ols_module/${scenario}" "${ols_module_port}"
    wait_for_url "http://127.0.0.1:${apache_port}/" || {
        matrix_fail "apache fixture server did not start"
        return 1
    }
    wait_for_url "http://127.0.0.1:${ols_module_port}/" || {
        matrix_fail "ols_module fixture server did not start"
        return 1
    }

    MATRIX_OUT_DIR="${out_dir}" \
    MATRIX_ENGINES="apache,ols_module" \
    MATRIX_APACHE_URL="http://127.0.0.1:${apache_port}" \
    MATRIX_OLS_MODULE_URL="http://127.0.0.1:${ols_module_port}" \
        bash "${SCRIPT_DIR}/run_matrix.sh" --scenario "${scenario}" "${args[@]}"

    python3 "${SCRIPT_DIR}/lib/validate_results.py" \
        --results-dir "${out_dir}/results" \
        --schema "${SCRIPT_DIR}/lib/result_schema.json"

    stop_fixture_servers
}

matrix_banner "Apps Matrix Fixture Smoke"
mkdir -p "${BASE_DIR}"

run_smoke_scenario wordpress-core \
    WP_CORE_001 WP_CORE_002 WP_CORE_003 WP_CORE_004 WP_CORE_005
run_smoke_scenario wordpress-redirection \
    WP_REDIRECT_001 WP_REDIRECT_002 WP_REDIRECT_003 WP_REDIRECT_004 WP_REDIRECT_005
run_smoke_scenario wordpress-w3-total-cache \
    WP_W3TC_002 WP_W3TC_003

matrix_ok "apps-matrix fixture smoke passed"
