#!/usr/bin/env bash
# Release-facing wrapper for apps-matrix budget ratcheting.
# By default this is advisory: insufficient samples are reported but do not
# block release preflight until APPS_MATRIX_BUDGET_RATCHET_REQUIRED=1.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${1:-${MATRIX_RATCHET_OUT_DIR:-${ROOT}/tests/e2e/apps-matrix/out}}"
REQUIRED="${APPS_MATRIX_BUDGET_RATCHET_REQUIRED:-0}"
UPDATE_ENV="${MATRIX_RATCHET_UPDATE_ENV:-0}"

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-apps-matrix-budget-ratchet.sh [artifact-dir]

Runs scripts/apps-matrix-ratchet-budgets.sh and turns its sample sufficiency
status into a release-readiness signal.

Environment:
  APPS_MATRIX_BUDGET_RATCHET_REQUIRED=0|1
      Default: 0. When 0, insufficient samples are reported as a warning.
      Set to 1 when budget enforcement is a release blocker.
  MATRIX_RATCHET_UPDATE_ENV=0|1
      Default: 0. When 1, insufficient samples always fail because budgets.env
      must not be updated from weak data.
  MATRIX_RATCHET_* variables are forwarded to apps-matrix-ratchet-budgets.sh.
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

case "$REQUIRED" in
    0|1) ;;
    *) fail "APPS_MATRIX_BUDGET_RATCHET_REQUIRED must be 0 or 1" ;;
esac
case "$UPDATE_ENV" in
    0|1) ;;
    *) fail "MATRIX_RATCHET_UPDATE_ENV must be 0 or 1" ;;
esac

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

set +e
bash "$ROOT/scripts/apps-matrix-ratchet-budgets.sh" "$OUT_DIR" >"$tmpdir/stdout" 2>"$tmpdir/stderr"
status="$?"
set -e

if [ -s "$tmpdir/stdout" ]; then
    cat "$tmpdir/stdout"
fi
if [ -s "$tmpdir/stderr" ]; then
    cat "$tmpdir/stderr" >&2
fi

case "$status" in
    0)
        printf 'OK: apps-matrix budget ratchet sample check passed.\n'
        ;;
    2)
        if [ "$REQUIRED" = "0" ] && [ "$UPDATE_ENV" = "0" ]; then
            printf 'WARN: apps-matrix budget ratchet has insufficient samples; budget enforcement remains disabled.\n' >&2
            exit 0
        fi
        fail "apps-matrix budget ratchet sample check failed with insufficient data"
        ;;
    *)
        fail "apps-matrix budget ratchet failed with status $status"
        ;;
esac
