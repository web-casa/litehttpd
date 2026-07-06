#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SCENARIOS_DIR="${SCRIPT_DIR}/scenarios"
MANIFEST="${SCRIPT_DIR}/MANIFEST.yaml"

# shellcheck source=tests/e2e/apps-matrix/lib/matrix_common.sh
source "${SCRIPT_DIR}/lib/matrix_common.sh"

PLAN_ONLY=0
INSTALL=0
INSTALL_ONLY=0
SCENARIO_FILTER=""
TIER="all"
CASE_IDS=()

usage() {
    cat <<EOF
Usage: $0 [--list] [--scenario <name>] [--case <id>] [--tier <tier>] [--install] [--install-only] [--plan-only]

  --list             List available scenarios
  --scenario <name>  Run a single scenario
  --case <id>        Run only one case id; may be passed multiple times
  --tier <tier>      all, pr, or nightly. pr uses MANIFEST.yaml cases_for_pr
  --install          Run scenario install/provision step before verification
  --install-only     Run install/provision step and skip verification
  --plan-only        Print plan only; do not attempt install/verify work
EOF
}

list_scenarios() {
    find "${SCENARIOS_DIR}" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort
}

join_case_ids() {
    local joined=""
    local case_id
    for case_id in "$@"; do
        if [[ -z "${joined}" ]]; then
            joined="${case_id}"
        else
            joined="${joined},${case_id}"
        fi
    done
    printf '%s' "${joined}"
}

manifest_should_run() {
    local scenario="$1"
    python3 "${SCRIPT_DIR}/lib/manifest_cases.py" \
        --manifest "${MANIFEST}" \
        --scenario "${scenario}" \
        --tier "${TIER}" \
        --should-run >/dev/null
}

manifest_case_ids() {
    local scenario="$1"
    python3 "${SCRIPT_DIR}/lib/manifest_cases.py" \
        --manifest "${MANIFEST}" \
        --scenario "${scenario}" \
        --tier "${TIER}" \
        --cases
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --list)
            list_scenarios
            exit 0
            ;;
        --scenario)
            SCENARIO_FILTER="$2"
            shift 2
            ;;
        --case)
            CASE_IDS+=("$2")
            shift 2
            ;;
        --tier)
            TIER="$2"
            case "${TIER}" in
                all|pr|nightly) ;;
                *)
                    echo "Unknown tier: ${TIER}"
                    usage
                    exit 1
                    ;;
            esac
            shift 2
            ;;
        --install)
            INSTALL=1
            shift
            ;;
        --install-only)
            INSTALL=1
            INSTALL_ONLY=1
            shift
            ;;
        --plan-only)
            PLAN_ONLY=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1"
            usage
            exit 1
            ;;
    esac
done

SUMMARY_CSV="${MATRIX_OUT_DIR}/summary.csv"
if [[ "${MATRIX_SUMMARY_APPEND:-0}" == "1" && -s "${SUMMARY_CSV}" ]]; then
    :
else
    matrix_summary_init "${SUMMARY_CSV}"
fi

matrix_banner "Apps Matrix Runner"

matched=0
for scenario_dir in "${SCENARIOS_DIR}"/*; do
    [[ -d "${scenario_dir}" ]] || continue
    scenario="$(basename "${scenario_dir}")"
    [[ -n "${SCENARIO_FILTER}" && "${SCENARIO_FILTER}" != "${scenario}" ]] && continue
    if ! manifest_should_run "${scenario}"; then
        continue
    fi
    matched=1

    matrix_banner "Scenario ${scenario}"
    if [[ "${PLAN_ONLY}" -eq 1 ]]; then
        matrix_print_case_plan "${scenario_dir}/cases.yaml"
        continue
    fi

    if [[ "${INSTALL}" -eq 1 ]]; then
        if [[ ! -x "${scenario_dir}/install.sh" ]]; then
            matrix_fail "missing install.sh for ${scenario}"
            exit 1
        fi
        "${scenario_dir}/install.sh" "${scenario}"
    fi

    if [[ "${INSTALL_ONLY}" -eq 1 ]]; then
        continue
    fi

    if [[ ! -x "${scenario_dir}/verify.sh" ]]; then
        matrix_fail "missing verify.sh for ${scenario}"
        exit 1
    fi

    explicit_case_ids="$(join_case_ids "${CASE_IDS[@]}")"
    tier_case_ids="$(manifest_case_ids "${scenario}")"
    if [[ -n "${explicit_case_ids}" ]]; then
        MATRIX_CASE_IDS="${explicit_case_ids}" "${scenario_dir}/verify.sh" "${scenario}" "${SUMMARY_CSV}"
    elif [[ -n "${tier_case_ids}" ]]; then
        MATRIX_CASE_IDS="${tier_case_ids}" "${scenario_dir}/verify.sh" "${scenario}" "${SUMMARY_CSV}"
    else
        "${scenario_dir}/verify.sh" "${scenario}" "${SUMMARY_CSV}"
    fi
done

if [[ "${matched}" -eq 0 ]]; then
    matrix_fail "no scenarios matched filter='${SCENARIO_FILTER:-<none>}' tier='${TIER}'"
    exit 1
fi

matrix_ok "summary written to ${SUMMARY_CSV}"
