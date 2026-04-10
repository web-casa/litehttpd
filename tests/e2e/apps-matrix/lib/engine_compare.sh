#!/usr/bin/env bash
set -euo pipefail

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
