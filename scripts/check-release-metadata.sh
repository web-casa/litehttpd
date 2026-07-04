#!/usr/bin/env bash
# Validate release tag, changelog, and GitHub Release note metadata.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${GITHUB_REF_NAME:-}"
MANIFEST_DIR=""

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-release-metadata.sh [--tag vX.Y.Z] [--manifest-dir DIR]

Checks that the release tag, spec versions, changelog heading, directive count,
and release-note template agree before a formal release workflow publishes
assets.
USAGE
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --tag)
            [ "$#" -ge 2 ] || fail "--tag requires a value"
            TAG="$2"
            shift
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
        *)
            usage >&2
            exit 2
            ;;
    esac
    shift
done

cd "$ROOT"

ols_version="$(awk '/^%global[[:space:]]+ols_version[[:space:]]+/ {print $3; exit}' packaging/openlitespeed-litehttpd.spec)"
release_version="$(awk '/^%global[[:space:]]+litehttpd_version[[:space:]]+/ {print $3; exit}' packaging/openlitespeed-litehttpd.spec)"
module_version="$(awk '/^Version:[[:space:]]+/ {print $2; exit}' packaging/litehttpd.spec)"
changelog_version="$(awk '/^## \[[0-9]+\.[0-9]+\.[0-9]+\]/ {sub(/^## \[/, ""); sub(/\].*/, ""); print; exit}' CHANGELOG.md)"
directive_count="$(awk '
    /typedef enum/ { in_enum=1 }
    in_enum && /DIR_[A-Z_]+/ {
        line=$0
        while (match(line, /DIR_[A-Z_]+/)) {
            name=substr(line, RSTART, RLENGTH)
            if (name != "DIR_NONE" && name != "DIR_COUNT") seen[name]=1
            line=substr(line, RSTART + RLENGTH)
        }
    }
    in_enum && /DIR_COUNT/ { in_enum=0 }
    END { for (name in seen) count++; print count + 0 }
' include/htaccess_directive.h)"

[ -n "$ols_version" ] || fail "could not read OLS version from openlitespeed spec"
[ -n "$release_version" ] || fail "could not read litehttpd release version from openlitespeed spec"
[ -n "$module_version" ] || fail "could not read module version from litehttpd spec"
[ -n "$changelog_version" ] || fail "could not read top changelog version"
[ "$changelog_version" = "$release_version" ] || \
    fail "CHANGELOG top version $changelog_version does not match release version $release_version"

if [ -n "$TAG" ]; then
    case "$TAG" in
        v*) tag_version="${TAG#v}" ;;
        *) fail "release tag must start with v, got: $TAG" ;;
    esac
    [ "$tag_version" = "$release_version" ] || \
        fail "release tag $TAG does not match release version $release_version"
fi

if [ "$directive_count" -lt 80 ]; then
    fail "directive count unexpectedly low: $directive_count"
fi

grep -Fq "Apache .htaccess compatibility module for OpenLiteSpeed (${directive_count} directive types" \
    .github/workflows/release.yml || \
    fail "release note template does not mention ${directive_count} directive types"
grep -Fq "litehttpd v__VERSION__" .github/workflows/release.yml || \
    fail "release note template missing version placeholder"
grep -Fq "openlitespeed-litehttpd-*.el8.<arch>.rpm" .github/workflows/release.yml || \
    fail "release note template missing RPM section"
grep -Fq "litehttpd-module-*-<arch>-linux.tgz" .github/workflows/release.yml || \
    fail "release note template missing module tarball section"

if [ -n "$MANIFEST_DIR" ]; then
    mkdir -p "$MANIFEST_DIR"
    {
        printf 'release_version=%s\n' "$release_version"
        printf 'ols_version=%s\n' "$ols_version"
        printf 'module_version=%s\n' "$module_version"
        printf 'changelog_version=%s\n' "$changelog_version"
        printf 'directive_count=%s\n' "$directive_count"
        if [ -n "$TAG" ]; then
            printf 'tag=%s\n' "$TAG"
        fi
    } >"$MANIFEST_DIR/release-metadata.env"
fi

printf 'OK: release metadata version=%s ols=%s module=%s directives=%s\n' \
    "$release_version" "$ols_version" "$module_version" "$directive_count"
