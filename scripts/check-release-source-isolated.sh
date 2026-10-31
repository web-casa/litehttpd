#!/usr/bin/env bash
# Verify that formal release packaging runs from a clean checkout, or prepare a
# clean detached worktree for manual release work.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ORIGINAL_CWD="$(pwd)"
REF="HEAD"
EXPECT_SHA=""
PREPARE_WORKTREE=""
MANIFEST_DIR=""

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-release-source-isolated.sh [options]

Checks that the release source tree is an isolated, clean checkout. This is the
guard for formal packaging: do not build release assets directly from a dirty
developer checkout.

Options:
  --ref REF              Release ref to validate or prepare. Default: HEAD.
  --expect-sha SHA       Require the checked source HEAD to equal SHA.
  --prepare-worktree DIR Create a clean detached worktree at DIR before checking.
  --manifest-dir DIR     Write source/scope manifests under DIR.
  -h, --help             Show this help.

Examples:
  bash scripts/check-release-source-isolated.sh --ref private/main
  bash scripts/check-release-source-isolated.sh --ref private/main \
    --prepare-worktree /tmp/litehttpd-release-main
USAGE
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

absolute_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$ORIGINAL_CWD" "$1" ;;
    esac
}

validate_sha() {
    local label="$1"
    local value="$2"

    if ! printf '%s' "$value" | grep -Eq '^[0-9a-f]{40}$'; then
        fail "$label must be a full 40-character git SHA, got: $value"
    fi
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --ref)
            [ "$#" -ge 2 ] || fail "--ref requires a value"
            REF="$2"
            shift
            ;;
        --expect-sha)
            [ "$#" -ge 2 ] || fail "--expect-sha requires a value"
            EXPECT_SHA="$2"
            shift
            ;;
        --prepare-worktree)
            [ "$#" -ge 2 ] || fail "--prepare-worktree requires a value"
            PREPARE_WORKTREE="$(absolute_path "$2")"
            shift
            ;;
        --manifest-dir)
            [ "$#" -ge 2 ] || fail "--manifest-dir requires a value"
            MANIFEST_DIR="$(absolute_path "$2")"
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
git rev-parse --verify HEAD >/dev/null

REF_SHA="$(git rev-parse --verify "${REF}^{commit}")" || fail "unknown release ref: $REF"
validate_sha "resolved ref SHA" "$REF_SHA"

if [ -n "$EXPECT_SHA" ]; then
    validate_sha "expected SHA" "$EXPECT_SHA"
    [ "$REF_SHA" = "$EXPECT_SHA" ] || fail "ref $REF resolved to $REF_SHA, expected $EXPECT_SHA"
fi

CHECK_ROOT="$ROOT"
if [ -n "$PREPARE_WORKTREE" ]; then
    [ ! -e "$PREPARE_WORKTREE" ] || fail "worktree path already exists: $PREPARE_WORKTREE"
    mkdir -p "$(dirname "$PREPARE_WORKTREE")"
    git worktree add --detach "$PREPARE_WORKTREE" "$REF_SHA" >/dev/null
    CHECK_ROOT="$PREPARE_WORKTREE"
fi

HEAD_SHA="$(git -C "$CHECK_ROOT" rev-parse --verify HEAD)"
validate_sha "source HEAD SHA" "$HEAD_SHA"
[ "$HEAD_SHA" = "$REF_SHA" ] || fail "source HEAD $HEAD_SHA does not match $REF ($REF_SHA)"

if [ -n "$EXPECT_SHA" ]; then
    [ "$HEAD_SHA" = "$EXPECT_SHA" ] || fail "source HEAD $HEAD_SHA does not match expected $EXPECT_SHA"
fi

status_file="$(mktemp)"
trap 'rm -f "$status_file"' EXIT
git -C "$CHECK_ROOT" status --porcelain=v1 --untracked-files=all >"$status_file"

if [ -n "$MANIFEST_DIR" ]; then
    mkdir -p "$MANIFEST_DIR"
    cp "$status_file" "$MANIFEST_DIR/git-status.txt"
    {
        printf 'release_ref=%q\n' "$REF"
        printf 'release_sha=%s\n' "$REF_SHA"
        printf 'source_sha=%s\n' "$HEAD_SHA"
        printf 'source_path=%q\n' "$CHECK_ROOT"
        if [ -n "$PREPARE_WORKTREE" ]; then
            printf 'prepared_worktree=1\n'
        else
            printf 'prepared_worktree=0\n'
        fi
    } >"$MANIFEST_DIR/source.env"
fi

if [ -s "$status_file" ]; then
    printf 'Dirty release source tree: %s\n' "$CHECK_ROOT" >&2
    sed 's/^/  /' "$status_file" >&2
    fail "formal release packaging requires a clean checkout or --prepare-worktree"
fi

if [ -n "$MANIFEST_DIR" ]; then
    (
        cd "$CHECK_ROOT"
        bash scripts/check-release-scope.sh --strict --manifest-dir "$MANIFEST_DIR/scope" >/dev/null
    )
fi

printf 'OK: release source is isolated and clean (%s at %s)\n' "$REF" "$HEAD_SHA"
if [ -n "$PREPARE_WORKTREE" ]; then
    printf 'Prepared worktree: %s\n' "$PREPARE_WORKTREE"
fi
