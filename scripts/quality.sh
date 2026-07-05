#!/usr/bin/env bash
# =============================================================================
# LiteHTTPD quality gate helper
#
# Default scope is "changed" so a new quality gate can be introduced without
# turning all historical style drift into an immediate release blocker.
#
# Examples:
#   bash scripts/quality.sh blocking
#   QUALITY_SCOPE=all bash scripts/quality.sh format-check
#   BUILD_DIR=build-quality bash scripts/quality.sh static-warn
#   bash scripts/quality.sh coverage
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

: "${QUALITY_SCOPE:=changed}"      # changed|all
: "${REQUIRE_TOOLS:=0}"            # 1 makes missing tools a hard failure
: "${BUILD_DIR:=build}"
: "${SHELLCHECK_SEVERITY:=warning}"

cd "$ROOT"

usage() {
    cat <<'USAGE'
Usage: bash scripts/quality.sh <command>

Commands:
  blocking       Patch consistency, release matrix, ShellCheck, and clang-format dry-run.
  release-preflight
                 Release scope, matrix, artifact report, RPM repo dry-run, and patch checks.
  release-ready  Strict release scope, full artifacts, full RPM repo, and patch checks.
  release-ready-staged
                 Run release-ready in a clean temp tree with only release-scoped dirty files.
  release-readiness
                 Release scope, matrix, package, repo, apps-matrix budget, optional DO gate.
  release-source-isolated
                 Require release packaging to run from a clean checkout/worktree.
  release-metadata
                 Validate tag, changelog, spec versions, and release-note metadata.
  release-public-sync
                 Report public mirror sync delta without mutating public-repo.
  release-github-gates
                 Check Dependabot, formal GitHub CI, and billing blockers.
  release-scope  Report dirty files inside/outside the release scope.
  release-matrix Validate release.yml matrix and release safety checks.
  release-artifacts
                 Validate release artifact names and matrix coverage.
  apps-matrix-budget-ratchet
                 Check apps-matrix p95 sample sufficiency before budget enforcement.
  apps-matrix-budget-policy
                 Validate advisory/enforced apps-matrix budget policy.
  apps-matrix-final-do-gate
                 Dispatch/watch the final apps-matrix DO release gate.
  assemble-release-tarballs
                 Derive patch/module tgz artifacts from full tgz artifacts.
  build-release-module-artifacts
                 Build release .so matrix and EL9 module RPMs with Docker.
  build-release-ols-artifacts
                 Build full OLS RPM/tgz artifacts with Docker and source cache.
  rpm-repo-dry-run
                 Build a local RPM repo from artifacts without uploading.
  static-warn    Advisory clang-tidy, cppcheck, and cmake-lint when available.
  coverage       Build with gcov/gcovr and write build-coverage/coverage.xml.
  format-check   clang-format dry-run only.
  format         clang-format in-place.
  shellcheck     ShellCheck only.
  patch-check    Patch and directive consistency checks only.

Environment:
  QUALITY_SCOPE=changed|all  Default: changed
  REQUIRE_TOOLS=0|1          Default: 0
  BUILD_DIR=<path>           Default: build
  SHELLCHECK_SEVERITY=<sev>  Default: warning
  RELEASE_SCOPE_STRICT=0|1   Default: 0; fail on out-of-scope dirty files.
  RELEASE_SCOPE_MANIFEST_DIR=path
                             Optional release scope manifest output dir.
  RELEASE_REQUIRE_FULL_ARTIFACTS=0|1
                             Default: 0; fail if artifact matrix is incomplete.
  RELEASE_ARTIFACT_OUT_DIR=path
                             Output dir for artifact builder helpers.
  RELEASE_READY_KEEP_STAGE=0|1
                             Keep staged release-ready temp tree for inspection.
  RPM_REPO_DRY_RUN_DIR=path  Default: /tmp/litehttpd-rpm-repo-dry-run.
  REPO_REQUIRE_FULL_MATRIX=0|1
                             Default: 0; fail if repo matrix is incomplete.
  APPS_MATRIX_BUDGET_RATCHET_REQUIRED=0|1
                             Default: 0; make insufficient budget samples fatal.
  APPS_MATRIX_BUDGET_ENFORCE_REQUESTED=0|1
                             Used by apps-matrix-budget-policy before enabling budgets.
  RELEASE_READINESS_*        Controls scripts/run-release-readiness.sh.
  RELEASE_GITHUB_*           Controls scripts/check-github-release-gates.sh.
USAGE
}

log() {
    printf '%s\n' "$*"
}

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

have_tool() {
    command -v "$1" >/dev/null 2>&1
}

need_tool() {
    local tool="$1"
    if have_tool "$tool"; then
        return 0
    fi
    if [ "$REQUIRE_TOOLS" = "1" ]; then
        die "missing required tool: $tool"
    fi
    log "SKIP: missing tool: $tool"
    return 1
}

is_zero_sha() {
    case "$1" in
        '' | 0000000000000000000000000000000000000000) return 0 ;;
        *) return 1 ;;
    esac
}

all_project_files() {
    git ls-files \
        .github \
        CMakeLists.txt \
        include \
        integration-tests \
        packaging \
        patches \
        scripts \
        src \
        tests \
        tools 2>/dev/null || true
    git ls-files --others --exclude-standard -- \
        .github \
        CMakeLists.txt \
        include \
        integration-tests \
        packaging \
        patches \
        scripts \
        src \
        tests \
        tools 2>/dev/null || true
}

changed_files() {
    local before="${GITHUB_EVENT_BEFORE:-}"
    local sha="${GITHUB_SHA:-HEAD}"
    local base="${QUALITY_BASE_REF:-}"

    if [ "$QUALITY_SCOPE" = "all" ]; then
        all_project_files | sort -u
        return 0
    fi

    if [ "$QUALITY_SCOPE" != "changed" ]; then
        die "QUALITY_SCOPE must be 'changed' or 'all', got: $QUALITY_SCOPE"
    fi

    if [ -n "$base" ] && git rev-parse --verify "$base" >/dev/null 2>&1; then
        git diff --name-only --diff-filter=ACMRT "$base"...HEAD
        return 0
    fi

    if [ -n "${GITHUB_BASE_REF:-}" ] &&
        git rev-parse --verify "origin/${GITHUB_BASE_REF}" >/dev/null 2>&1; then
        git diff --name-only --diff-filter=ACMRT "origin/${GITHUB_BASE_REF}"...HEAD
        return 0
    fi

    if ! is_zero_sha "$before" && git cat-file -e "${before}^{commit}" 2>/dev/null; then
        git diff --name-only --diff-filter=ACMRT "$before" "$sha"
        return 0
    fi

    {
        git diff --name-only --diff-filter=ACMRT
        git diff --cached --name-only --diff-filter=ACMRT
        git ls-files --others --exclude-standard
    } | sort -u
}

project_scoped_files() {
    local file
    changed_files | while IFS= read -r file; do
        [ -n "$file" ] || continue
        [ -f "$file" ] || continue
        case "$file" in
            build/* | Testing/* | docs/node_modules/* | public-repo/*)
                continue
                ;;
        esac
        case "$file" in
            .github/* | CMakeLists.txt | include/* | integration-tests/* | packaging/* | patches/* | scripts/* | src/* | tests/* | tools/*)
                printf '%s\n' "$file"
                ;;
        esac
    done | sort -u
}

c_family_files() {
    project_scoped_files | while IFS= read -r file; do
        case "$file" in
            *.c | *.cc | *.cpp | *.cxx | *.h | *.hh | *.hpp | *.hxx)
                printf '%s\n' "$file"
                ;;
        esac
    done
}

c_source_files() {
    project_scoped_files | while IFS= read -r file; do
        case "$file" in
            *.c | *.cc | *.cpp | *.cxx)
                printf '%s\n' "$file"
                ;;
        esac
    done
}

shell_files() {
    project_scoped_files | while IFS= read -r file; do
        case "$file" in
            *.sh)
                printf '%s\n' "$file"
                ;;
        esac
    done
}

cmake_files() {
    project_scoped_files | while IFS= read -r file; do
        case "$file" in
            CMakeLists.txt | *.cmake)
                printf '%s\n' "$file"
                ;;
        esac
    done
}

readarray_or_empty() {
    local -n dest="$1"
    dest=()
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        dest+=("$line")
    done
}

run_patch_checks() {
    log "==> Patch consistency"
    bash tests/check_patches.sh
    bash tests/check_consistency.sh
}

run_release_matrix_check() {
    log "==> Release matrix"
    bash scripts/check-release-matrix.sh
}

run_shellcheck() {
    local files=()
    need_tool shellcheck || return 0
    readarray_or_empty files < <(shell_files)
    if [ "${#files[@]}" -eq 0 ]; then
        log "SKIP: no shell files in $QUALITY_SCOPE scope"
        return 0
    fi
    log "==> ShellCheck (${#files[@]} files)"
    shellcheck --severity="$SHELLCHECK_SEVERITY" "${files[@]}"
}

run_clang_format_check() {
    local files=()
    need_tool clang-format || return 0
    readarray_or_empty files < <(c_family_files)
    if [ "${#files[@]}" -eq 0 ]; then
        log "SKIP: no C/C++ files in $QUALITY_SCOPE scope"
        return 0
    fi
    log "==> clang-format dry-run (${#files[@]} files)"
    clang-format --dry-run --Werror "${files[@]}"
}

run_clang_format_fix() {
    local files=()
    need_tool clang-format || return 0
    readarray_or_empty files < <(c_family_files)
    if [ "${#files[@]}" -eq 0 ]; then
        log "SKIP: no C/C++ files in $QUALITY_SCOPE scope"
        return 0
    fi
    log "==> clang-format in-place (${#files[@]} files)"
    clang-format -i "${files[@]}"
}

run_clang_tidy() {
    local files=()
    local compile_dir="${COMPILE_COMMANDS_DIR:-$BUILD_DIR}"

    need_tool clang-tidy || return 0
    if [ ! -f "$compile_dir/compile_commands.json" ]; then
        log "SKIP: no compile_commands.json under $compile_dir"
        return 0
    fi

    readarray_or_empty files < <(c_source_files)
    if [ "${#files[@]}" -eq 0 ]; then
        log "SKIP: no C/C++ source files in $QUALITY_SCOPE scope"
        return 0
    fi

    log "==> clang-tidy (${#files[@]} files)"
    clang-tidy -p "$compile_dir" --quiet "${files[@]}"
}

run_cppcheck() {
    local files=()

    need_tool cppcheck || return 0
    readarray_or_empty files < <(c_source_files)
    if [ "${#files[@]}" -eq 0 ]; then
        log "SKIP: no C/C++ source files in $QUALITY_SCOPE scope"
        return 0
    fi

    log "==> cppcheck (${#files[@]} files)"
    cppcheck "${files[@]}" \
        -I include \
        -I tests \
        --enable=warning,style,performance,portability \
        --inline-suppr \
        --suppress=missingIncludeSystem
}

run_cmake_lint() {
    local files=()
    need_tool cmake-lint || return 0
    readarray_or_empty files < <(cmake_files)
    if [ "${#files[@]}" -eq 0 ]; then
        log "SKIP: no CMake files in $QUALITY_SCOPE scope"
        return 0
    fi
    log "==> cmake-lint (${#files[@]} files)"
    cmake-lint "${files[@]}"
}

run_static_warn() {
    local rc=0
    run_clang_tidy || rc=1
    run_cppcheck || rc=1
    run_cmake_lint || rc=1
    return "$rc"
}

run_coverage() {
    local build_dir="${COVERAGE_BUILD_DIR:-build-coverage}"

    need_tool cmake || return 0
    need_tool gcovr || return 0

    log "==> Configure coverage build"
    cmake -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_FLAGS="--coverage -O0 -g" \
        -DCMAKE_CXX_FLAGS="--coverage -O0 -g" \
        -DCMAKE_EXE_LINKER_FLAGS="--coverage" \
        -DCMAKE_SHARED_LINKER_FLAGS="--coverage"

    log "==> Build coverage targets"
    cmake --build "$build_dir" --parallel "$(nproc)"

    log "==> Run coverage tests"
    "./$build_dir/tests/unit_tests" --gtest_brief=1
    "./$build_dir/tests/property_tests" --gtest_brief=1
    "./$build_dir/tests/compat_tests" --gtest_brief=1
    "./$build_dir/tests/confconv_tests" --gtest_brief=1

    log "==> gcovr summary"
    gcovr --root "$ROOT" \
        --filter "$ROOT/src/" \
        --filter "$ROOT/tools/" \
        --exclude '.*/_deps/.*' \
        --exclude '.*/tests/.*' \
        --print-summary \
        --xml-pretty \
        -o "$build_dir/coverage.xml"
}

run_blocking() {
    run_patch_checks
    run_release_matrix_check
    run_shellcheck
    run_clang_format_check
}

run_release_preflight() {
    bash scripts/check-release-scope.sh
    bash scripts/check-release-matrix.sh
    bash scripts/check-release-artifacts.sh
    run_patch_checks
    bash scripts/check-rpm-repo-dry-run.sh
}

run_release_ready() {
    bash scripts/check-release-source-isolated.sh
    bash scripts/check-release-metadata.sh
    RELEASE_SCOPE_STRICT=1 bash scripts/check-release-scope.sh
    bash scripts/check-public-release-sync.sh
    bash scripts/check-release-matrix.sh
    RELEASE_REQUIRE_FULL_ARTIFACTS=1 bash scripts/check-release-artifacts.sh
    run_patch_checks
    REPO_REQUIRE_FULL_MATRIX=1 bash scripts/check-rpm-repo-dry-run.sh
    bash scripts/check-apps-matrix-budget-policy.sh
}

run_release_ready_staged() {
    bash scripts/run-release-ready-staged.sh
}

run_release_readiness() {
    bash scripts/run-release-readiness.sh "$@"
}

run_apps_matrix_budget_ratchet() {
    bash scripts/check-apps-matrix-budget-ratchet.sh "$@"
}

run_apps_matrix_budget_policy() {
    bash scripts/check-apps-matrix-budget-policy.sh "$@"
}

run_apps_matrix_final_do_gate() {
    bash scripts/apps-matrix-final-do-gate.sh "$@"
}

command="${1:-blocking}"
if [ "$#" -gt 0 ]; then
    shift
fi

case "$command" in
    blocking)
        run_blocking
        ;;
    release-preflight)
        run_release_preflight
        ;;
    release-ready)
        run_release_ready
        ;;
    release-ready-staged)
        run_release_ready_staged
        ;;
    release-readiness)
        run_release_readiness "$@"
        ;;
    release-source-isolated)
        bash scripts/check-release-source-isolated.sh "$@"
        ;;
    release-metadata)
        bash scripts/check-release-metadata.sh "$@"
        ;;
    release-public-sync)
        bash scripts/check-public-release-sync.sh "$@"
        ;;
    release-github-gates)
        bash scripts/check-github-release-gates.sh "$@"
        ;;
    release-scope)
        bash scripts/check-release-scope.sh
        ;;
    release-matrix)
        bash scripts/check-release-matrix.sh
        ;;
    release-artifacts)
        bash scripts/check-release-artifacts.sh
        ;;
    apps-matrix-budget-ratchet)
        run_apps_matrix_budget_ratchet "$@"
        ;;
    apps-matrix-budget-policy)
        run_apps_matrix_budget_policy "$@"
        ;;
    apps-matrix-final-do-gate)
        run_apps_matrix_final_do_gate "$@"
        ;;
    assemble-release-tarballs)
        bash scripts/assemble-release-tarballs.sh
        ;;
    build-release-module-artifacts)
        bash scripts/build-release-module-artifacts.sh
        ;;
    build-release-ols-artifacts)
        bash scripts/build-release-ols-artifacts.sh
        ;;
    rpm-repo-dry-run)
        bash scripts/check-rpm-repo-dry-run.sh
        ;;
    static-warn)
        run_static_warn
        ;;
    coverage)
        run_coverage
        ;;
    format-check)
        run_clang_format_check
        ;;
    format)
        run_clang_format_fix
        ;;
    shellcheck)
        run_shellcheck
        ;;
    patch-check)
        run_patch_checks
        ;;
    -h | --help | help)
        usage
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
