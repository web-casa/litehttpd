#!/usr/bin/env bash
set -euo pipefail

MATRIX_COMPARE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MATRIX_DIR="${MATRIX_DIR:-$(cd "${MATRIX_COMPARE_DIR}/.." && pwd)}"
MATRIX_OUT_DIR="${MATRIX_OUT_DIR:-${MATRIX_DIR}/out}"

APACHE_URL="${APACHE_URL:-http://localhost:18080}"
OLS_MODULE_URL="${OLS_MODULE_URL:-http://localhost:38080}"
OLS_NATIVE_URL="${OLS_NATIVE_URL:-http://localhost:28080}"

engine_url() {
    case "$1" in
        apache) echo "${APACHE_URL}" ;;
        ols_module) echo "${OLS_MODULE_URL}" ;;
        ols_native) echo "${OLS_NATIVE_URL}" ;;
        *) return 1 ;;
    esac
}

fetch_case_artifact() {
    local engine="$1"
    local method="$2"
    local path="$3"
    local out_dir="$4"
    local body_file headers_file status_file
    local base

    base="$(engine_url "${engine}")"
    mkdir -p "${out_dir}"
    body_file="${out_dir}/body.txt"
    headers_file="${out_dir}/headers.txt"
    status_file="${out_dir}/status.txt"

    curl -sS -X "${method}" \
        --max-time 20 \
        -D "${headers_file}" \
        -o "${body_file}" \
        "${base}${path}" >/dev/null 2>&1 || true

    awk 'NR==1 { print $2 }' "${headers_file}" > "${status_file}"
}

header_value() {
    local headers_file="$1"
    local name="$2"
    awk -F': ' -v key="${name}" 'tolower($1) == tolower(key) { gsub("\r", "", $2); print $2; exit }' "${headers_file}"
}

status_value() {
    local status_file="$1"
    tr -d '\r\n' < "${status_file}"
}

ttl_from_headers() {
    local headers_file="$1"
    local cc
    cc="$(header_value "${headers_file}" "Cache-Control" || true)"
    if [[ "${cc}" =~ max-age=([0-9]+) ]]; then
        echo "${BASH_REMATCH[1]}"
        return 0
    fi
    return 1
}

write_case_json_stub() {
    local scenario="$1"
    local case_id="$2"
    local out_file="$3"
    cat >"${out_file}" <<EOF
{
  "scenario": "${scenario}",
  "case_id": "${case_id}",
  "result": "PASS_KNOWN_DIFF",
  "known_diff_reason": "executor stub only",
  "risk_level": "low"
}
EOF
}

matrix_run_cases() {
    local scenario="$1"
    local cases_yaml="$2"
    local summary_csv="$3"
    local args=()
    local case_id

    if ! command -v python3 >/dev/null 2>&1; then
        matrix_fail "python3 is required for apps-matrix execution"
        return 1
    fi

    if [[ -n "${MATRIX_CASE_IDS:-}" ]]; then
        IFS=',' read -r -a matrix_case_ids <<< "${MATRIX_CASE_IDS}"
        for case_id in "${matrix_case_ids[@]}"; do
            [[ -n "${case_id}" ]] || continue
            args+=(--case-id "${case_id}")
        done
    fi

    python3 "${MATRIX_DIR}/lib/run_cases.py" \
        --scenario "${scenario}" \
        --cases "${cases_yaml}" \
        "${args[@]}" \
        --summary "${summary_csv}" \
        --out-dir "${MATRIX_OUT_DIR}"
}
