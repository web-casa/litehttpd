#!/usr/bin/env bash
# Report whether the public mirror tree needs a sync from the private release
# source, without modifying the working public-repo checkout.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REF="HEAD"
MANIFEST_DIR=""
STRICT=0

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-public-release-sync.sh [options]

Builds a temporary public mirror tree from a release ref using the same managed
path rules as public-repo/sync.sh, then compares it with the public-repo tree
tracked at that ref. This is a report by default; use --strict to fail when the
public mirror needs a follow-up sync commit.

Options:
  --ref REF            Release ref to inspect. Default: HEAD.
  --manifest-dir DIR   Write summary and public-sync.patch under DIR.
  --strict             Fail if the public mirror tree would change.
  -h, --help           Show this help.
USAGE
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --ref)
            [ "$#" -ge 2 ] || fail "--ref requires a value"
            REF="$2"
            shift
            ;;
        --manifest-dir)
            [ "$#" -ge 2 ] || fail "--manifest-dir requires a value"
            MANIFEST_DIR="$2"
            shift
            ;;
        --strict)
            STRICT=1
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
REF_SHA="$(git rev-parse --verify "${REF}^{commit}")" || fail "unknown release ref: $REF"

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
mkdir -p "$tmpdir/private" "$tmpdir/before" "$tmpdir/after"
git archive --format=tar "$REF_SHA" | tar xf - -C "$tmpdir/private"

[ -d "$tmpdir/private/public-repo/litehttpd" ] || fail "public-repo/litehttpd missing at $REF_SHA"
cp -a "$tmpdir/private/public-repo/litehttpd/." "$tmpdir/before/"
cp -a "$tmpdir/private/public-repo/litehttpd/." "$tmpdir/after/"

managed_dirs="src include tools tests docs patches"
for managed_dir in $managed_dirs; do
    rm -rf "$tmpdir/after/$managed_dir"
    if [ -d "$tmpdir/private/$managed_dir" ]; then
        cp -a "$tmpdir/private/$managed_dir" "$tmpdir/after/"
    fi
done

for file in CMakeLists.txt CHANGELOG.md LICENSE; do
    if [ -e "$tmpdir/private/$file" ]; then
        cp -a "$tmpdir/private/$file" "$tmpdir/after/"
    fi
done

rm -f "$tmpdir/after/docs/rpm-build-lessons.md"
find "$tmpdir/after" -name .git -prune -o -type d \
    \( -name node_modules -o -name dist -o -name out -o -name build \
       -o -name __pycache__ -o -name _deps -o -name CMakeFiles \) \
    -exec rm -rf {} +
find "$tmpdir/after" -name .git -prune -o -type f \
    \( -name '*.so' -o -name '*.o' -o -name '*.a' -o -name '*.dylib' \
       -o -name '*.gcda' -o -name '*.gcno' -o -name '*.gcov' -o -name '*.pyc' \
       -o -name '*.rpm' -o -name '*.deb' -o -name '*.tar.*' \) \
    -exec rm -f {} +

diff_status=0
diff -ruN "$tmpdir/before" "$tmpdir/after" >"$tmpdir/public-sync.patch" || diff_status="$?"
case "$diff_status" in
    0|1) ;;
    *) fail "diff failed with status $diff_status" ;;
esac

set +e
changed_paths="$(
    diff -qr "$tmpdir/before" "$tmpdir/after" 2>/dev/null | wc -l | tr -d ' '
)"
set -e
sync_needed=0
if [ "$changed_paths" -gt 0 ]; then
    sync_needed=1
fi

if [ -n "$MANIFEST_DIR" ]; then
    mkdir -p "$MANIFEST_DIR"
    cp "$tmpdir/public-sync.patch" "$MANIFEST_DIR/public-sync.patch"
    {
        printf 'release_ref=%q\n' "$REF"
        printf 'release_sha=%s\n' "$REF_SHA"
        printf 'sync_needed=%s\n' "$sync_needed"
        printf 'changed_paths=%s\n' "$changed_paths"
    } >"$MANIFEST_DIR/summary.env"
fi

if git rev-parse --verify origin/main >/dev/null 2>&1; then
    if git merge-base --is-ancestor origin/main "$REF_SHA"; then
        printf 'Public origin/main is an ancestor of %s; direct fast-forward may be possible.\n' "$REF_SHA"
    else
        printf 'INFO: origin/main diverges from %s; use public-repo sync flow, not a direct private-history merge.\n' "$REF_SHA"
    fi
fi

if [ "$sync_needed" = "1" ]; then
    printf 'WARN: public mirror sync report has %s changed path(s).\n' "$changed_paths" >&2
    if [ -n "$MANIFEST_DIR" ]; then
        printf 'Public sync patch: %s/public-sync.patch\n' "$MANIFEST_DIR" >&2
    fi
    if [ "$STRICT" = "1" ]; then
        fail "public mirror requires a sync commit"
    fi
else
    printf 'OK: public mirror tree is in sync for %s\n' "$REF_SHA"
fi
