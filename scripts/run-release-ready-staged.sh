#!/usr/bin/env bash
# Run the release-ready gate in a temporary clean worktree containing only
# release-scoped dirty files from the current checkout.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KEEP_STAGE="${RELEASE_READY_KEEP_STAGE:-0}"
STAGE_DIR="${RELEASE_READY_STAGE_DIR:-}"
MANIFEST_DIR="${RELEASE_READY_MANIFEST_DIR:-}"

usage() {
    cat <<'USAGE'
Usage: bash scripts/run-release-ready-staged.sh

Creates a temporary clean checkout from HEAD, overlays only files classified by
scripts/check-release-scope.sh as release scope, then runs:
  bash scripts/quality.sh release-ready

Environment:
  RELEASE_READY_KEEP_STAGE=1      Keep temporary stage after the run.
  RELEASE_READY_STAGE_DIR=path    Use this stage directory instead of mktemp.
  RELEASE_READY_MANIFEST_DIR=path Write the release scope manifest here.
USAGE
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    usage
    exit 0
fi
[ "$#" -eq 0 ] || {
    usage >&2
    exit 2
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

copy_or_remove_path() {
    local path="$1"
    local src="$ROOT/$path"
    local dst="$STAGE_DIR/$path"

    if [ -e "$src" ] || [ -L "$src" ]; then
        mkdir -p "$(dirname "$dst")"
        rm -rf "$dst"
        cp -a "$src" "$dst"
    else
        rm -rf "$dst"
    fi
}

cleanup() {
    if [ "$KEEP_STAGE" = "1" ]; then
        echo "Stage kept: $STAGE_DIR"
    else
        rm -rf "$STAGE_DIR"
    fi
    if [ -z "${RELEASE_READY_MANIFEST_DIR:-}" ]; then
        rm -rf "$MANIFEST_DIR"
    fi
}

cd "$ROOT"
git rev-parse --verify HEAD >/dev/null

if [ -z "$MANIFEST_DIR" ]; then
    MANIFEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/litehttpd-release-scope.XXXXXX")"
fi
bash scripts/check-release-scope.sh --manifest-dir "$MANIFEST_DIR"

if [ -z "$STAGE_DIR" ]; then
    STAGE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/litehttpd-release-ready.XXXXXX")"
else
    [ ! -e "$STAGE_DIR" ] || die "stage dir already exists: $STAGE_DIR"
    mkdir -p "$STAGE_DIR"
fi
trap cleanup EXIT

git archive --format=tar HEAD | tar xf - -C "$STAGE_DIR"
(
    cd "$STAGE_DIR"
    git init -q
    # git archive only contains tracked files from HEAD. Force-add them so a
    # legacy ignore rule cannot drop tracked files from the synthetic base.
    git add -A -f
    git -c user.name=release-ready -c user.email=release-ready@example.invalid \
        commit -q -m "release-ready base"
)

while IFS= read -r path; do
    [ -n "$path" ] || continue
    copy_or_remove_path "$path"
done < "$MANIFEST_DIR/in-scope.txt"

echo "========================================"
echo " Release Ready Staged Gate"
echo "========================================"
echo "Stage:    $STAGE_DIR"
echo "Manifest: $MANIFEST_DIR"
echo "========================================"

(
    cd "$STAGE_DIR"
    bash scripts/quality.sh release-ready
)
