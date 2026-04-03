#!/usr/bin/env bash
set -euo pipefail

SCENARIO="$1"
SUMMARY_CSV="$2"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# shellcheck source=tests/e2e/apps-matrix/lib/matrix_common.sh
source "${ROOT_DIR}/lib/matrix_common.sh"

matrix_warn "${SCENARIO}: scaffold only; execution hook not implemented yet"
matrix_print_case_plan "${SCRIPT_DIR}/cases.yaml"
matrix_summary_append "${SUMMARY_CSV}" "${SCENARIO}" "PLAN_ONLY" "meta" "PASS_KNOWN_DIFF" "scenario scaffold created; executor pending"
