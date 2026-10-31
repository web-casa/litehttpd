#!/usr/bin/env bash
set -euo pipefail

SCENARIO="$1"
SUMMARY_CSV="$2"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# shellcheck source=tests/e2e/apps-matrix/lib/matrix_common.sh
source "${ROOT_DIR}/lib/matrix_common.sh"
# shellcheck source=tests/e2e/apps-matrix/lib/engine_compare.sh
source "${ROOT_DIR}/lib/engine_compare.sh"

matrix_run_cases "${SCENARIO}" "${SCRIPT_DIR}/cases.yaml" "${SUMMARY_CSV}"
