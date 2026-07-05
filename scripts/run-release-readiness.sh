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
CHECK_SOURCE_ISOLATION="${RELEASE_READINESS_CHECK_SOURCE_ISOLATION:-0}"
CHECK_GITHUB_GATES="${RELEASE_READINESS_CHECK_GITHUB_GATES:-0}"
RERUN_GITHUB_GATES="${RELEASE_READINESS_RERUN_GITHUB_GATES:-0}"
WATCH_GITHUB_GATES="${RELEASE_READINESS_WATCH_GITHUB_GATES:-0}"
RELEASE_REF="${RELEASE_READINESS_REF:-HEAD}"
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
  --check-github-gates       Check Dependabot and formal GitHub workflow gates.
  --rerun-github-gates       Rerun/dispatch formal GitHub workflow gates before
                             checking them. Implies --check-github-gates.
  --watch-github-gates       Wait for formal GitHub workflow gates to complete.
                             Implies --check-github-gates.
  --manifest-dir DIR         Write release scope manifests here.
  --check-source-isolation   Require this checkout to be clean release source.
  --release-ref REF          Ref used by source/public-sync checks. Default: HEAD.
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
        --check-github-gates)
            CHECK_GITHUB_GATES=1
            ;;
        --rerun-github-gates)
            CHECK_GITHUB_GATES=1
            RERUN_GITHUB_GATES=1
            ;;
        --watch-github-gates)
            CHECK_GITHUB_GATES=1
            WATCH_GITHUB_GATES=1
            ;;
        --manifest-dir)
            [ "$#" -ge 2 ] || fail "--manifest-dir requires a value"
            MANIFEST_DIR="$2"
            shift
            ;;
        --check-source-isolation)
            CHECK_SOURCE_ISOLATION=1
            ;;
        --release-ref)
            [ "$#" -ge 2 ] || fail "--release-ref requires a value"
            RELEASE_REF="$2"
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
validate_bool RELEASE_READINESS_CHECK_SOURCE_ISOLATION "$CHECK_SOURCE_ISOLATION"
validate_bool RELEASE_READINESS_CHECK_GITHUB_GATES "$CHECK_GITHUB_GATES"
validate_bool RELEASE_READINESS_RERUN_GITHUB_GATES "$RERUN_GITHUB_GATES"
validate_bool RELEASE_READINESS_WATCH_GITHUB_GATES "$WATCH_GITHUB_GATES"

scope_args=(--manifest-dir "$MANIFEST_DIR/scope")
if [ "$STRICT_SCOPE" = "1" ]; then
    scope_args=(--strict "${scope_args[@]}")
fi

if [ "$CHECK_SOURCE_ISOLATION" = "1" ]; then
    run_step bash scripts/check-release-source-isolated.sh \
        --ref "$RELEASE_REF" \
        --manifest-dir "$MANIFEST_DIR/source"
fi

run_step bash scripts/check-release-metadata.sh --manifest-dir "$MANIFEST_DIR/metadata"
run_step bash scripts/check-public-release-sync.sh --ref "$RELEASE_REF" --manifest-dir "$MANIFEST_DIR/public-sync"
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

run_step bash scripts/check-apps-matrix-budget-policy.sh

if [ "$CHECK_GITHUB_GATES" = "1" ]; then
    github_gate_args=(--ref "$RELEASE_REF")
    if [ "$RERUN_GITHUB_GATES" = "1" ]; then
        github_gate_args+=(--rerun-formal)
    fi
    if [ "$WATCH_GITHUB_GATES" = "1" ]; then
        github_gate_args+=(--watch)
    fi
    run_step bash scripts/check-github-release-gates.sh "${github_gate_args[@]}"
else
    printf '\nSKIP: GitHub formal CI gate disabled. Use --check-github-gates after billing is unblocked.\n'
fi

if [ "$RUN_DO_GATE" = "1" ]; then
    do_gate_env=()
    if [ -z "${APPS_MATRIX_FINAL_DO_REF:-}" ]; then
        do_gate_env+=(APPS_MATRIX_FINAL_DO_REF="$RELEASE_REF")
    fi
    run_step env "${do_gate_env[@]}" bash scripts/apps-matrix-final-do-gate.sh
else
    printf '\nSKIP: DO release gate dispatch disabled. Use --run-do-gate for the final cloud gate.\n'
fi
