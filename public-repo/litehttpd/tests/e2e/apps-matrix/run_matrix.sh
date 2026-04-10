#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SCENARIOS_DIR="${SCRIPT_DIR}/scenarios"

# shellcheck source=tests/e2e/apps-matrix/lib/matrix_common.sh
source "${SCRIPT_DIR}/lib/matrix_common.sh"

PLAN_ONLY=0
SCENARIO_FILTER=""

usage() {
    cat <<EOF
Usage: $0 [--list] [--scenario <name>] [--plan-only]

  --list             List available scenarios
  --scenario <name>  Run a single scenario
  --plan-only        Print plan only; do not attempt install/verify work
EOF
}

list_scenarios() {
    find "${SCENARIOS_DIR}" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort
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
matrix_summary_init "${SUMMARY_CSV}"

matrix_banner "Apps Matrix Runner"

for scenario_dir in "${SCENARIOS_DIR}"/*; do
    [[ -d "${scenario_dir}" ]] || continue
    scenario="$(basename "${scenario_dir}")"
    [[ -n "${SCENARIO_FILTER}" && "${SCENARIO_FILTER}" != "${scenario}" ]] && continue

    matrix_banner "Scenario ${scenario}"
    if [[ "${PLAN_ONLY}" -eq 1 ]]; then
        matrix_print_case_plan "${scenario_dir}/cases.yaml"
        continue
    fi

    if [[ ! -x "${scenario_dir}/verify.sh" ]]; then
        matrix_fail "missing verify.sh for ${scenario}"
        exit 1
    fi

    "${scenario_dir}/verify.sh" "${scenario}" "${SUMMARY_CSV}"
done

matrix_ok "summary written to ${SUMMARY_CSV}"
