#!/usr/bin/env bash
# One command for the final release readiness pass.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

STRICT_SCOPE="${RELEASE_READINESS_STRICT_SCOPE:-0}"
REQUIRE_FULL_ARTIFACTS="${RELEASE_READINESS_REQUIRE_FULL_ARTIFACTS:-0}"
RUN_DO_GATE="${RELEASE_READINESS_RUN_DO_GATE:-0}"
BUILD_ARTIFACTS="${RELEASE_READINESS_BUILD_ARTIFACTS:-0}"
RUN_RPM_REPO="${RELEASE_READINESS_RUN_RPM_REPO:-1}"
MANIFEST_DIR="${RELEASE_READINESS_MANIFEST_DIR:-out/release-readiness}"
ARTIFACT_DIRS=()

usage() {
    cat <<'USAGE'
Usage: bash scripts/run-release-readiness.sh [options] [artifact-dir ...]

Runs the release branch hygiene checks, release matrix validation, package
artifact validation, RPM repo dry-run, and apps-matrix budget ratchet audit.
The final DO release gate is opt-in because it dispatches a real GitHub/DO run.

Options:
  --strict-scope             Fail if out-of-scope dirty files are present.
  --require-full-artifacts   Fail if any release artifact matrix slot is missing.
  --build-artifacts          Build release artifacts locally before validation.
  --skip-rpm-repo            Skip RPM repo dry-run.
  --run-do-gate              Dispatch apps-matrix final DO gate.
  --manifest-dir DIR         Write release scope manifests here.
  -h, --help                 Show this help.

DO gate options are passed via APPS_MATRIX_FINAL_DO_* environment variables or
directly by running scripts/apps-matrix-final-do-gate.sh.
USAGE
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

run_step() {
    printf '\n==> %s\n' "$*"
    "$@"
}

validate_bool() {
    local name="$1"
    local value="$2"

    case "$value" in
        0|1) ;;
        *) fail "$name must be 0 or 1, got: $value" ;;
    esac
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --strict-scope)
            STRICT_SCOPE=1
            ;;
        --require-full-artifacts)
            REQUIRE_FULL_ARTIFACTS=1
            ;;
        --build-artifacts)
            BUILD_ARTIFACTS=1
            ;;
        --skip-rpm-repo)
            RUN_RPM_REPO=0
            ;;
        --run-do-gate)
            RUN_DO_GATE=1
            ;;
        --manifest-dir)
            [ "$#" -ge 2 ] || fail "--manifest-dir requires a value"
            MANIFEST_DIR="$2"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --*)
            usage >&2
            exit 2
            ;;
        *)
            ARTIFACT_DIRS+=("$1")
            ;;
    esac
    shift
done

validate_bool RELEASE_READINESS_STRICT_SCOPE "$STRICT_SCOPE"
validate_bool RELEASE_READINESS_REQUIRE_FULL_ARTIFACTS "$REQUIRE_FULL_ARTIFACTS"
validate_bool RELEASE_READINESS_RUN_DO_GATE "$RUN_DO_GATE"
validate_bool RELEASE_READINESS_BUILD_ARTIFACTS "$BUILD_ARTIFACTS"
validate_bool RELEASE_READINESS_RUN_RPM_REPO "$RUN_RPM_REPO"

scope_args=(--manifest-dir "$MANIFEST_DIR/scope")
if [ "$STRICT_SCOPE" = "1" ]; then
    scope_args=(--strict "${scope_args[@]}")
fi

run_step bash scripts/check-release-scope.sh "${scope_args[@]}"
run_step bash scripts/check-release-matrix.sh
run_step bash tests/check_patches.sh
run_step bash tests/check_consistency.sh

if [ "$BUILD_ARTIFACTS" = "1" ]; then
    run_step bash scripts/build-release-module-artifacts.sh
    run_step bash scripts/build-release-ols-artifacts.sh
fi

artifact_args=("${ARTIFACT_DIRS[@]}")
if [ "$REQUIRE_FULL_ARTIFACTS" = "1" ]; then
    run_step env RELEASE_REQUIRE_FULL_ARTIFACTS=1 bash scripts/check-release-artifacts.sh "${artifact_args[@]}"
else
    run_step bash scripts/check-release-artifacts.sh "${artifact_args[@]}"
fi

if [ "$RUN_RPM_REPO" = "1" ]; then
    if [ "$REQUIRE_FULL_ARTIFACTS" = "1" ]; then
        run_step env REPO_REQUIRE_FULL_MATRIX=1 bash scripts/check-rpm-repo-dry-run.sh "${artifact_args[@]}"
    else
        run_step bash scripts/check-rpm-repo-dry-run.sh "${artifact_args[@]}"
    fi
else
    printf '\nSKIP: RPM repo dry-run disabled.\n'
fi

run_step bash scripts/check-apps-matrix-budget-ratchet.sh

if [ "$RUN_DO_GATE" = "1" ]; then
    run_step bash scripts/apps-matrix-final-do-gate.sh
else
    printf '\nSKIP: DO release gate dispatch disabled. Use --run-do-gate for the final cloud gate.\n'
fi
