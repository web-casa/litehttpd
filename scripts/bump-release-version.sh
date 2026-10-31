#!/usr/bin/env bash
# Prepare a release metadata version bump after the current release tag is
# already occupied. Dry-run is the default; pass --write to modify files.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

MODE="dry-run"
TARGET_VERSION=""
BUMP_KIND=""
RELEASE_DATE="${RELEASE_VERSION_DATE:-$(date +%F)}"
REPO="${RELEASE_VERSION_REPO:-${GH_REPO:-}}"
CHECK_REMOTE_TAG="${RELEASE_VERSION_CHECK_REMOTE_TAG:-1}"
ALLOW_EXISTING_TAG="${RELEASE_VERSION_ALLOW_EXISTING_TAG:-0}"
CHANGELOG_MODE="${RELEASE_VERSION_CHANGELOG_MODE:-prepend}"
CHANGELOG_NOTE="${RELEASE_VERSION_CHANGELOG_NOTE:-Release metadata and final release gates for v__VERSION__.}"
MANIFEST_DIR="${RELEASE_VERSION_MANIFEST_DIR:-}"
TARGET_FILES=(
    "packaging/openlitespeed-litehttpd.spec"
    "CHANGELOG.md"
)

usage() {
    cat <<'USAGE'
Usage: bash scripts/bump-release-version.sh (--version X.Y.Z | --patch | --minor | --major) [options]

Updates release metadata after a release tag conflict. The default mode is a
dry-run diff; pass --write to modify files.

Options:
  --version X.Y.Z            Set the exact release version.
  --patch                    Increment the current patch version.
  --minor                    Increment the minor version and reset patch to 0.
  --major                    Increment the major version and reset minor/patch.
  --date YYYY-MM-DD          Changelog date. Default: today.
  --repo OWNER/REPO          GitHub repository used to check tag availability.
                             Default: GH_REPO, private remote, then origin.
  --skip-remote-tag-check    Do not query GitHub for vX.Y.Z availability.
  --allow-existing-tag       Allow the target tag when it already exists at
                             HEAD. Intended only for idempotent retries.
  --changelog-mode MODE      prepend or replace. Default: prepend.
  --changelog-note TEXT      One-line note for prepended changelog section.
                             __VERSION__ is replaced with X.Y.Z.
  --manifest-dir DIR         Write release-version-bump.env.
  --write                    Modify files. Without this, only prints a diff.
  -h, --help                 Show this help.

Environment mirrors options with RELEASE_VERSION_* variables.
USAGE
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

validate_bool() {
    local name="$1"
    local value="$2"

    case "$value" in
        0|1) ;;
        *) fail "$name must be 0 or 1, got: $value" ;;
    esac
}

validate_version() {
    local version="$1"

    if ! printf '%s' "$version" | grep -Eq '^[0-9]+[.][0-9]+[.][0-9]+$'; then
        fail "version must look like X.Y.Z, got: $version"
    fi
}

validate_date() {
    local release_date="$1"

    if ! printf '%s' "$release_date" | grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2}$'; then
        fail "date must look like YYYY-MM-DD, got: $release_date"
    fi
}

validate_changelog_mode() {
    case "$CHANGELOG_MODE" in
        prepend|replace) ;;
        *) fail "changelog mode must be prepend or replace, got: $CHANGELOG_MODE" ;;
    esac
}

validate_changelog_note() {
    case "$CHANGELOG_NOTE" in
        *$'\n'*|*$'\r'*)
            fail "changelog note must be a single line"
            ;;
    esac
    [ -n "$CHANGELOG_NOTE" ] || fail "changelog note cannot be empty"
}

github_repo_from_remote() {
    local remote="$1"
    local url path

    url="$(git remote get-url "$remote" 2>/dev/null || true)"
    [ -n "$url" ] || return 0

    case "$url" in
        https://github.com/*)
            path="${url#https://github.com/}"
            ;;
        git@github.com:*)
            path="${url#git@github.com:}"
            ;;
        ssh://git@github.com/*)
            path="${url#ssh://git@github.com/}"
            ;;
        *)
            return 0
            ;;
    esac
    path="${path%.git}"
    if printf '%s' "$path" | grep -Eq '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$'; then
        printf '%s\n' "$path"
    fi
}

read_release_version() {
    awk '/^%global[[:space:]]+litehttpd_version[[:space:]]+/ {print $3; exit}' \
        packaging/openlitespeed-litehttpd.spec
}

read_changelog_version() {
    awk '/^## \[[0-9]+\.[0-9]+\.[0-9]+\]/ {
        sub(/^## \[/, "")
        sub(/\].*/, "")
        print
        exit
    }' CHANGELOG.md
}

bump_version() {
    local current="$1"
    local kind="$2"
    local major minor patch

    IFS=. read -r major minor patch <<< "$current"
    case "$kind" in
        patch)
            patch=$((10#$patch + 1))
            ;;
        minor)
            minor=$((10#$minor + 1))
            patch=0
            ;;
        major)
            major=$((10#$major + 1))
            minor=0
            patch=0
            ;;
        *)
            fail "unknown bump kind: $kind"
            ;;
    esac
    printf '%s.%s.%s\n' "$major" "$minor" "$patch"
}

remote_tag_sha() {
    local tag="$1"
    local object_sha object_type peeled_sha

    object_sha="$(gh api "repos/$REPO/git/ref/tags/$tag" --jq '.object.sha' 2>/dev/null || true)"
    object_type="$(gh api "repos/$REPO/git/ref/tags/$tag" --jq '.object.type' 2>/dev/null || true)"
    if ! printf '%s' "$object_sha" | grep -Eq '^[0-9a-f]{40}$'; then
        return 0
    fi

    if [ "$object_type" = "tag" ]; then
        peeled_sha="$(
            gh api "repos/$REPO/git/tags/$object_sha" --jq '.object.sha' 2>/dev/null || true
        )"
        if printf '%s' "$peeled_sha" | grep -Eq '^[0-9a-f]{40}$'; then
            printf '%s\n' "$peeled_sha"
            return 0
        fi
    fi

    printf '%s\n' "$object_sha"
}

local_tag_sha() {
    local tag="$1"

    git rev-parse -q --verify "refs/tags/$tag^{commit}" 2>/dev/null || true
}

check_target_files_clean() {
    local file

    for file in "${TARGET_FILES[@]}"; do
        [ -f "$file" ] || fail "required file missing: $file"
        git diff --quiet -- "$file" || fail "refusing to overwrite dirty file: $file"
        git diff --cached --quiet -- "$file" || fail "refusing to overwrite staged file: $file"
    done
}

apply_version_update() {
    local spec_file="$1"
    local changelog_file="$2"
    local old_version="$3"
    local new_version="$4"
    local release_date="$5"
    local changelog_mode="$6"
    local changelog_note="$7"

    OLD_VERSION="$old_version" NEW_VERSION="$new_version" perl -0pi -e '
        s/^(%global[ \t]+litehttpd_version[ \t]+)\Q$ENV{OLD_VERSION}\E\b/${1}$ENV{NEW_VERSION}/m
            or die "litehttpd_version not found\n";
    ' "$spec_file"

    if [ "$changelog_mode" = "replace" ]; then
        OLD_VERSION="$old_version" NEW_VERSION="$new_version" RELEASE_DATE="$release_date" perl -0pi -e '
            s/^## \[\Q$ENV{OLD_VERSION}\E\] - [0-9]{4}-[0-9]{2}-[0-9]{2}/## [$ENV{NEW_VERSION}] - $ENV{RELEASE_DATE}/m
                or die "top changelog heading not found\n";
        ' "$changelog_file"
    else
        OLD_VERSION="$old_version" NEW_VERSION="$new_version" RELEASE_DATE="$release_date" CHANGELOG_NOTE="$changelog_note" perl -0pi -e '
            my $note = $ENV{CHANGELOG_NOTE};
            $note =~ s/__VERSION__/$ENV{NEW_VERSION}/g;
            my $section = "## [$ENV{NEW_VERSION}] - $ENV{RELEASE_DATE}\n\n"
                . "### Changed\n"
                . "- $note\n\n";
            s/^(## \[\Q$ENV{OLD_VERSION}\E\] - [0-9]{4}-[0-9]{2}-[0-9]{2})/$section$1/m
                or die "top changelog heading not found\n";
        ' "$changelog_file"
    fi
}

print_dry_run_diff() {
    local old_version="$1"
    local new_version="$2"
    local release_date="$3"
    local tmpdir
    local diff_status=0

    tmpdir="$(mktemp -d)"
    mkdir -p "$tmpdir/packaging"
    cp packaging/openlitespeed-litehttpd.spec "$tmpdir/packaging/openlitespeed-litehttpd.spec"
    cp CHANGELOG.md "$tmpdir/CHANGELOG.md"

    apply_version_update \
        "$tmpdir/packaging/openlitespeed-litehttpd.spec" \
        "$tmpdir/CHANGELOG.md" \
        "$old_version" \
        "$new_version" \
        "$release_date" \
        "$CHANGELOG_MODE" \
        "$CHANGELOG_NOTE"

    diff -u \
        --label packaging/openlitespeed-litehttpd.spec \
        --label packaging/openlitespeed-litehttpd.spec \
        packaging/openlitespeed-litehttpd.spec \
        "$tmpdir/packaging/openlitespeed-litehttpd.spec" || diff_status="$?"
    case "$diff_status" in
        0|1) ;;
        *) fail "diff failed with status $diff_status" ;;
    esac

    diff_status=0
    diff -u \
        --label CHANGELOG.md \
        --label CHANGELOG.md \
        CHANGELOG.md \
        "$tmpdir/CHANGELOG.md" || diff_status="$?"
    case "$diff_status" in
        0|1) ;;
        *) fail "diff failed with status $diff_status" ;;
    esac

    rm -rf "$tmpdir"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --version)
            [ "$#" -ge 2 ] || fail "--version requires a value"
            TARGET_VERSION="$2"
            shift
            ;;
        --patch|--minor|--major)
            [ -z "$BUMP_KIND" ] || fail "only one bump kind can be selected"
            BUMP_KIND="${1#--}"
            ;;
        --date)
            [ "$#" -ge 2 ] || fail "--date requires a value"
            RELEASE_DATE="$2"
            shift
            ;;
        --repo)
            [ "$#" -ge 2 ] || fail "--repo requires a value"
            REPO="$2"
            shift
            ;;
        --skip-remote-tag-check)
            CHECK_REMOTE_TAG=0
            ;;
        --allow-existing-tag)
            ALLOW_EXISTING_TAG=1
            ;;
        --changelog-mode)
            [ "$#" -ge 2 ] || fail "--changelog-mode requires a value"
            CHANGELOG_MODE="$2"
            shift
            ;;
        --changelog-note)
            [ "$#" -ge 2 ] || fail "--changelog-note requires a value"
            CHANGELOG_NOTE="$2"
            shift
            ;;
        --manifest-dir)
            [ "$#" -ge 2 ] || fail "--manifest-dir requires a value"
            MANIFEST_DIR="$2"
            shift
            ;;
        --write)
            MODE="write"
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

validate_bool RELEASE_VERSION_CHECK_REMOTE_TAG "$CHECK_REMOTE_TAG"
validate_bool RELEASE_VERSION_ALLOW_EXISTING_TAG "$ALLOW_EXISTING_TAG"
validate_date "$RELEASE_DATE"
validate_changelog_mode
validate_changelog_note

[ -n "$TARGET_VERSION" ] || [ -n "$BUMP_KIND" ] || fail "choose --version, --patch, --minor, or --major"
if [ -n "$TARGET_VERSION" ] && [ -n "$BUMP_KIND" ]; then
    fail "choose either --version or a bump kind, not both"
fi

current_version="$(read_release_version)"
changelog_version="$(read_changelog_version)"
[ -n "$current_version" ] || fail "could not read release version from openlitespeed spec"
[ -n "$changelog_version" ] || fail "could not read top changelog version"
[ "$current_version" = "$changelog_version" ] || \
    fail "CHANGELOG top version $changelog_version does not match release version $current_version"
validate_version "$current_version"

if [ -z "$TARGET_VERSION" ]; then
    TARGET_VERSION="$(bump_version "$current_version" "$BUMP_KIND")"
fi
validate_version "$TARGET_VERSION"
[ "$TARGET_VERSION" != "$current_version" ] || fail "target version already matches current version"

target_tag="v$TARGET_VERSION"
head_sha="$(git rev-parse --verify HEAD)"

if [ "$CHECK_REMOTE_TAG" = "1" ]; then
    command -v gh >/dev/null 2>&1 || fail "missing required tool for remote tag check: gh"
    if [ -z "$REPO" ]; then
        REPO="$(github_repo_from_remote private)"
    fi
    if [ -z "$REPO" ]; then
        REPO="$(github_repo_from_remote origin)"
    fi
    [ -n "$REPO" ] || fail "could not resolve GitHub repo; pass --repo OWNER/REPO"
    if ! printf '%s' "$REPO" | grep -Eq '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$'; then
        fail "GitHub repo must be OWNER/REPO, got: $REPO"
    fi

    existing_remote="$(remote_tag_sha "$target_tag")"
    if [ -n "$existing_remote" ]; then
        if [ "$ALLOW_EXISTING_TAG" = "1" ] && [ "$existing_remote" = "$head_sha" ]; then
            printf 'OK: remote tag %s already points at HEAD %s.\n' "$target_tag" "$head_sha"
        else
            fail "remote tag $target_tag already exists at $existing_remote"
        fi
    else
        printf 'OK: remote tag %s is available in %s.\n' "$target_tag" "$REPO"
    fi
else
    printf 'SKIP: remote tag availability check disabled for %s.\n' "$target_tag"
fi

existing_local="$(local_tag_sha "$target_tag")"
if [ -n "$existing_local" ]; then
    if [ "$ALLOW_EXISTING_TAG" = "1" ] && [ "$existing_local" = "$head_sha" ]; then
        printf 'OK: local tag %s already points at HEAD %s.\n' "$target_tag" "$head_sha"
    else
        fail "local tag $target_tag already exists at $existing_local"
    fi
fi

if [ "$MODE" = "write" ]; then
    check_target_files_clean
    apply_version_update \
        packaging/openlitespeed-litehttpd.spec \
        CHANGELOG.md \
        "$current_version" \
        "$TARGET_VERSION" \
        "$RELEASE_DATE" \
        "$CHANGELOG_MODE" \
        "$CHANGELOG_NOTE"
    printf 'Updated release metadata: %s -> %s (%s).\n' \
        "$current_version" "$TARGET_VERSION" "$RELEASE_DATE"
else
    printf 'DRY RUN: release metadata would update %s -> %s (%s).\n' \
        "$current_version" "$TARGET_VERSION" "$RELEASE_DATE"
    print_dry_run_diff "$current_version" "$TARGET_VERSION" "$RELEASE_DATE"
    printf '\nRun with --write to modify files.\n'
fi

if [ -n "$MANIFEST_DIR" ]; then
    mkdir -p "$MANIFEST_DIR"
    {
        printf 'mode=%q\n' "$MODE"
        printf 'current_version=%s\n' "$current_version"
        printf 'target_version=%s\n' "$TARGET_VERSION"
        printf 'target_tag=%s\n' "$target_tag"
        printf 'release_date=%s\n' "$RELEASE_DATE"
        printf 'changelog_mode=%q\n' "$CHANGELOG_MODE"
        printf 'changelog_note=%q\n' "$CHANGELOG_NOTE"
        printf 'repo=%q\n' "$REPO"
        printf 'remote_tag_check=%s\n' "$CHECK_REMOTE_TAG"
        printf 'head_sha=%s\n' "$head_sha"
    } >"$MANIFEST_DIR/release-version-bump.env"
fi
