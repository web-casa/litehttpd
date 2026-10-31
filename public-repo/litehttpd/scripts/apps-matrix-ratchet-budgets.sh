#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${1:-${MATRIX_RATCHET_OUT_DIR:-${ROOT_DIR}/tests/e2e/apps-matrix/out}}"
BUDGET_FILE="${MATRIX_RATCHET_BUDGET_FILE:-${ROOT_DIR}/tests/e2e/apps-matrix/config/budgets.env}"
MIN_SAMPLES="${MATRIX_RATCHET_MIN_SAMPLES:-5}"
REQUIRE_SCENARIOS="${MATRIX_RATCHET_REQUIRE_SCENARIOS:-heavy}"
REQUIRE_PHASES="${MATRIX_RATCHET_REQUIRE_PHASES:-all}"
UPDATE_ENV="${MATRIX_RATCHET_UPDATE_ENV:-0}"
PERCENTILE="${MATRIX_RATCHET_PERCENTILE:-95}"
MULTIPLIER="${MATRIX_RATCHET_MULTIPLIER:-1.35}"

usage() {
    cat <<EOF
Usage: $0 [artifact-dir]

Checks that apps-matrix timing artifacts have enough successful samples, prints
budget suggestions, and optionally rewrites config/budgets.env.

Environment:
  MATRIX_RATCHET_OUT_DIR             Artifact root. Default: tests/e2e/apps-matrix/out
  MATRIX_RATCHET_BUDGET_FILE         Budget env file. Default: tests/e2e/apps-matrix/config/budgets.env
  MATRIX_RATCHET_MIN_SAMPLES         Minimum ok samples per required scenario/phase. Default: 5
  MATRIX_RATCHET_REQUIRE_SCENARIOS   heavy, wordpress, cms, framework, all, or comma list. Default: heavy
  MATRIX_RATCHET_REQUIRE_PHASES      install, verify, all, or comma list. Default: all
  MATRIX_RATCHET_UPDATE_ENV          0 or 1. Set to 1 to update budgets.env. Default: 0
  MATRIX_RATCHET_PERCENTILE          Percentile to budget from. Default: 95
  MATRIX_RATCHET_MULTIPLIER          Safety multiplier. Default: 1.35
EOF
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

case "${UPDATE_ENV}" in
    0|1) ;;
    *) fail "MATRIX_RATCHET_UPDATE_ENV must be 0 or 1" ;;
esac
if [[ ! "${MIN_SAMPLES}" =~ ^[1-9][0-9]*$ ]]; then
    fail "MATRIX_RATCHET_MIN_SAMPLES must be a positive integer"
fi

args=(
    --out-dir "${OUT_DIR}"
    --include-per-engine
    --min-samples "${MIN_SAMPLES}"
    --percentile "${PERCENTILE}"
    --multiplier "${MULTIPLIER}"
    --require-scenarios "${REQUIRE_SCENARIOS}"
    --require-phases "${REQUIRE_PHASES}"
)

if [[ "${UPDATE_ENV}" == "1" ]]; then
    args+=(--update-env "${BUDGET_FILE}")
fi

python3 "${ROOT_DIR}/tests/e2e/apps-matrix/lib/tune_budgets.py" "${args[@]}"

if [[ "${UPDATE_ENV}" == "1" ]]; then
    printf 'Updated %s after sample validation.\n' "${BUDGET_FILE}"
else
    printf '# dry-run only; set MATRIX_RATCHET_UPDATE_ENV=1 to update %s\n' "${BUDGET_FILE}"
fi
