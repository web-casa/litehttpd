#!/usr/bin/env bash
# Classify dirty worktree files into the 1.9.1 release scope and everything
# else. Use --strict before publishing to prevent unrelated changes from
# leaking into a release commit.

set -euo pipefail

STRICT="${RELEASE_SCOPE_STRICT:-0}"
MANIFEST_DIR="${RELEASE_SCOPE_MANIFEST_DIR:-}"

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-release-scope.sh [--strict] [--manifest-dir DIR]

Reports dirty worktree files that are inside/outside the 1.9.1 release scope.
With --strict or RELEASE_SCOPE_STRICT=1, fails if any out-of-scope file is dirty.
With --manifest-dir or RELEASE_SCOPE_MANIFEST_DIR, writes in-scope/generated/
out-of-scope manifests for release review.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --strict)
            STRICT=1
            ;;
        --manifest-dir)
            [ "$#" -ge 2 ] || {
                usage >&2
                exit 2
            }
            MANIFEST_DIR="$2"
            shift
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
    shift
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

is_release_scope() {
    local path="$1"

    case "$path" in
        .clang-format | \
        .clang-tidy | \
        .gitignore | \
        CMakeLists.txt | \
        .github/workflows/ci.yml | \
        .github/workflows/apps-matrix-heavy.yml | \
        .github/workflows/apps-matrix-do-release-gate.yml | \
        .github/workflows/apps-matrix-runtime-images.yml | \
        .github/workflows/codeql.yml | \
        .github/workflows/nightly.yml | \
        .github/workflows/release.yml | \
        DIGITALOCEAN_LA_REALITY_TEST_2026-07-02.md | \
        NETCUP_ARM_RUNTIME_TEST_2026-07-02.md | \
        docs/rpm-build-lessons.md | \
        integration-tests/docker-compose.prebuilt.yml | \
        integration-tests/Dockerfile.ols | \
        integration-tests/apps/*/verify.sh | \
        patches/0001-lsiapi-phpconfig.patch | \
        patches/0002-lsiapi-rewrite.patch | \
        patches/0003-readapacheconf.patch | \
        patches/0004-autoindex-403.patch | \
        patches/README.md | \
        packaging/litehttpd.spec | \
        packaging/openlitespeed-litehttpd.spec | \
        packaging/setup-repo.sh | \
        packaging/generate-index.sh | \
        packaging/tarball/install-full.sh | \
        packaging/tarball/install-patch.sh | \
        scripts/assemble-release-tarballs.sh | \
        scripts/apps-matrix-final-do-gate.sh | \
        scripts/apps-matrix-ratchet-budgets.sh | \
        scripts/apps-matrix-do-prepare-base-image.sh | \
        scripts/apps-matrix-do-vps-test.sh | \
        scripts/build-release-module-artifacts.sh | \
        scripts/build-release-ols-artifacts.sh | \
        scripts/check-apps-matrix-budget-policy.sh | \
        scripts/check-apps-matrix-budget-ratchet.sh | \
        scripts/check-github-release-gates.sh | \
        scripts/check-release-matrix.sh | \
        scripts/check-release-artifacts.sh | \
        scripts/check-release-metadata.sh | \
        scripts/check-release-source-isolated.sh | \
        scripts/check-release-tag-gate.sh | \
        scripts/check-release-scope.sh | \
        scripts/check-public-release-sync.sh | \
        scripts/check-rpm-repo-dry-run.sh | \
        scripts/create-release-source-cache.sh | \
        scripts/run-release-readiness.sh | \
        scripts/run-release-ready-staged.sh | \
        scripts/quality.sh | \
        src/htaccess_cache.c | \
        src/htaccess_exec_auth.c | \
        tests/check_consistency.sh | \
        tests/check_patches.sh | \
        tests/e2e/compare/Dockerfile.ols-module | \
        tests/e2e/Dockerfile.custom-ols | \
        tests/e2e/Dockerfile.custom-ols.dockerignore | \
        tests/e2e/apps-matrix/* | \
        tests/e2e/vps-5engine/deploy.sh | \
        tests/generators/gen_directive.h | \
        tests/property/prop_generators_smoke.cpp | \
        tests/unit/test_cache.cpp)
            return 0
            ;;
    esac

    return 1
}

is_generated_scope() {
    local path="$1"

    case "$path" in
        build/* | \
        build-*/* | \
        Testing/* | \
        .ninja_* | \
        CMakeFiles/* | \
        CMakeCache.txt | \
        compile_commands.json)
            return 0
            ;;
    esac

    return 1
}

status_paths() {
    local line path

    git status --porcelain=v1 --untracked-files=all | while IFS= read -r line; do
        [ -n "$line" ] || continue
        path="${line:3}"
        case "$path" in
            *" -> "*) path="${path##* -> }" ;;
        esac
        printf '%s\n' "$path"
    done | sort -u
}

in_scope_file="$(mktemp)"
generated_file="$(mktemp)"
out_scope_file="$(mktemp)"
trap 'rm -f "$in_scope_file" "$generated_file" "$out_scope_file"' EXIT

while IFS= read -r path; do
    [ -n "$path" ] || continue
    if is_release_scope "$path"; then
        printf '%s\n' "$path" >> "$in_scope_file"
    elif is_generated_scope "$path"; then
        printf '%s\n' "$path" >> "$generated_file"
    else
        printf '%s\n' "$path" >> "$out_scope_file"
    fi
done < <(status_paths)

in_count="$(wc -l < "$in_scope_file" | tr -d ' ')"
generated_count="$(wc -l < "$generated_file" | tr -d ' ')"
out_count="$(wc -l < "$out_scope_file" | tr -d ' ')"

echo "========================================"
echo " Release Scope Check"
echo "========================================"
echo "In-scope dirty files:  $in_count"
if [ "$in_count" -gt 0 ]; then
    sed 's/^/  /' "$in_scope_file"
fi
echo "Generated dirty files: $generated_count"
if [ "$generated_count" -gt 0 ]; then
    sed 's/^/  /' "$generated_file"
fi
echo "Out-of-scope dirty files: $out_count"
if [ "$out_count" -gt 0 ]; then
    sed 's/^/  /' "$out_scope_file"
fi

if [ -n "$MANIFEST_DIR" ]; then
    mkdir -p "$MANIFEST_DIR"
    cp "$in_scope_file" "$MANIFEST_DIR/in-scope.txt"
    cp "$generated_file" "$MANIFEST_DIR/generated.txt"
    cp "$out_scope_file" "$MANIFEST_DIR/out-of-scope.txt"
    {
        echo "in_scope=$in_count"
        echo "generated=$generated_count"
        echo "out_of_scope=$out_count"
    } > "$MANIFEST_DIR/summary.env"
    echo "Manifest dir: $MANIFEST_DIR"
fi

if [ "$STRICT" = "1" ] && [ "$out_count" -gt 0 ]; then
    echo "ERROR: out-of-scope dirty files are present" >&2
    exit 1
fi

echo "========================================"
