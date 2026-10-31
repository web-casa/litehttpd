#!/usr/bin/env bash
# Validate the release budget policy and then run the apps-matrix ratchet audit.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
POLICY_FILE="${APPS_MATRIX_BUDGET_POLICY_FILE:-$ROOT/tests/e2e/apps-matrix/config/budget-policy.env}"
OUT_DIR="${1:-${MATRIX_RATCHET_OUT_DIR:-$ROOT/tests/e2e/apps-matrix/out}}"
ENFORCE_REQUESTED="${APPS_MATRIX_BUDGET_ENFORCE_REQUESTED:-0}"
UPDATE_ENV="${MATRIX_RATCHET_UPDATE_ENV:-0}"

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-apps-matrix-budget-policy.sh [artifact-dir]

Validates config/budget-policy.env and runs the budget ratchet check in advisory
or required mode according to policy.

Environment:
  APPS_MATRIX_BUDGET_POLICY_FILE       Policy file path.
  APPS_MATRIX_BUDGET_ENFORCE_REQUESTED 0|1, set by workflows before enabling
                                       MATRIX_ENFORCE_BUDGETS.
  MATRIX_RATCHET_UPDATE_ENV           0|1, forwarded; updating budgets requires
                                       sufficient samples.
USAGE
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

case "${1:-}" in
    -h|--help)
        usage
        exit 0
        ;;
esac

[ -f "$POLICY_FILE" ] || fail "budget policy file not found: $POLICY_FILE"
# shellcheck disable=SC1090
. "$POLICY_FILE"

: "${APPS_MATRIX_BUDGET_POLICY_MODE:=advisory}"
: "${APPS_MATRIX_BUDGET_ALLOW_ENFORCEMENT:=0}"
: "${APPS_MATRIX_BUDGET_REQUIRED_MIN_SAMPLES:=5}"
: "${APPS_MATRIX_BUDGET_REQUIRED_SCENARIOS:=heavy}"
: "${APPS_MATRIX_BUDGET_REQUIRED_PHASES:=all}"

case "$APPS_MATRIX_BUDGET_POLICY_MODE" in
    advisory|enforced) ;;
    *) fail "APPS_MATRIX_BUDGET_POLICY_MODE must be advisory or enforced" ;;
esac
case "$APPS_MATRIX_BUDGET_ALLOW_ENFORCEMENT" in
    0|1) ;;
    *) fail "APPS_MATRIX_BUDGET_ALLOW_ENFORCEMENT must be 0 or 1" ;;
esac
case "$ENFORCE_REQUESTED" in
    0|1) ;;
    *) fail "APPS_MATRIX_BUDGET_ENFORCE_REQUESTED must be 0 or 1" ;;
esac
case "$UPDATE_ENV" in
    0|1) ;;
    *) fail "MATRIX_RATCHET_UPDATE_ENV must be 0 or 1" ;;
esac
if ! printf '%s' "$APPS_MATRIX_BUDGET_REQUIRED_MIN_SAMPLES" | grep -Eq '^[1-9][0-9]*$'; then
    fail "APPS_MATRIX_BUDGET_REQUIRED_MIN_SAMPLES must be a positive integer"
fi

if [ "$ENFORCE_REQUESTED" = "1" ]; then
    [ "$APPS_MATRIX_BUDGET_POLICY_MODE" = "enforced" ] || \
        fail "budget enforcement requested while policy mode is advisory"
    [ "$APPS_MATRIX_BUDGET_ALLOW_ENFORCEMENT" = "1" ] || \
        fail "budget enforcement requested but APPS_MATRIX_BUDGET_ALLOW_ENFORCEMENT is not 1"
fi

required=0
if [ "$APPS_MATRIX_BUDGET_POLICY_MODE" = "enforced" ] || \
    [ "$ENFORCE_REQUESTED" = "1" ] || \
    [ "$UPDATE_ENV" = "1" ]; then
    required=1
fi

printf 'Apps-matrix budget policy: mode=%s enforcement_allowed=%s min_samples=%s scenarios=%s phases=%s\n' \
    "$APPS_MATRIX_BUDGET_POLICY_MODE" \
    "$APPS_MATRIX_BUDGET_ALLOW_ENFORCEMENT" \
    "$APPS_MATRIX_BUDGET_REQUIRED_MIN_SAMPLES" \
    "$APPS_MATRIX_BUDGET_REQUIRED_SCENARIOS" \
    "$APPS_MATRIX_BUDGET_REQUIRED_PHASES"

env \
    APPS_MATRIX_BUDGET_RATCHET_REQUIRED="$required" \
    MATRIX_RATCHET_MIN_SAMPLES="$APPS_MATRIX_BUDGET_REQUIRED_MIN_SAMPLES" \
    MATRIX_RATCHET_REQUIRE_SCENARIOS="$APPS_MATRIX_BUDGET_REQUIRED_SCENARIOS" \
    MATRIX_RATCHET_REQUIRE_PHASES="$APPS_MATRIX_BUDGET_REQUIRED_PHASES" \
    MATRIX_RATCHET_UPDATE_ENV="$UPDATE_ENV" \
    bash "$ROOT/scripts/check-apps-matrix-budget-ratchet.sh" "$OUT_DIR"
