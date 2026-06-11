#!/bin/bash
# Sync public-repo from litehttpd-private
# Usage: bash public-repo/sync.sh
#
# Copies the public-facing subset of the private repo into the public mirror
# checkout at public-repo/litehttpd (which holds its own .git). It refreshes
# only the managed paths below — files that exist solely in the public repo
# (e.g. README.md, README_CN.md, .gitignore) are left untouched — and strips
# build artifacts that `cp -r` would otherwise drag in.

set -euo pipefail
PRIVATE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PUBLIC_DIR="$PRIVATE_DIR/public-repo/litehttpd"

# Safety: never operate on an empty/unset target path.
if [ -z "$PUBLIC_DIR" ] || [ "$PUBLIC_DIR" = "/" ]; then
    echo "FATAL: refusing to sync to '$PUBLIC_DIR'" >&2
    exit 1
fi

echo "=== Syncing public repo from private ==="
echo "Private: $PRIVATE_DIR"
echo "Public:  $PUBLIC_DIR"

mkdir -p "$PUBLIC_DIR"

# Managed directories: replace each in place. We remove only the managed dir
# (not the whole checkout), so the public repo's .git and any public-only files
# survive — no .git stash/restore dance, and no risk of deleting README etc.
MANAGED_DIRS="src include tools tests docs patches"
for d in $MANAGED_DIRS; do
    rm -rf "${PUBLIC_DIR:?}/$d"
    if [ -d "$PRIVATE_DIR/$d" ]; then
        cp -r "$PRIVATE_DIR/$d" "$PUBLIC_DIR/"
    fi
done

# Managed top-level files.
cp "$PRIVATE_DIR/CMakeLists.txt" "$PUBLIC_DIR/"
cp "$PRIVATE_DIR/CHANGELOG.md"   "$PUBLIC_DIR/"
cp "$PRIVATE_DIR/LICENSE"        "$PUBLIC_DIR/" 2>/dev/null || true

# Drop docs that are private-only / not meant for the public site.
rm -f "$PUBLIC_DIR/docs/rpm-build-lessons.md"

# Strip build artifacts and vendored deps that cp -r may have copied in
# (they are gitignored in the private repo, but cp ignores .gitignore).
find "$PUBLIC_DIR" -name .git -prune -o -type d \
    \( -name node_modules -o -name dist -o -name out -o -name build \
       -o -name __pycache__ -o -name _deps -o -name CMakeFiles \) \
    -exec rm -rf {} +
# Use -exec rm (not -delete): -delete implies -depth, which disables -prune.
find "$PUBLIC_DIR" -name .git -prune -o -type f \
    \( -name '*.so' -o -name '*.o' -o -name '*.a' -o -name '*.dylib' \
       -o -name '*.gcda' -o -name '*.gcno' -o -name '*.gcov' -o -name '*.pyc' \
       -o -name '*.rpm' -o -name '*.deb' -o -name '*.tar.*' \) \
    -exec rm -f {} +

# Count
echo ""
echo "=== Synced ==="
echo "Source files: $(find "$PUBLIC_DIR/src" -name '*.c' -o -name '*.h' | wc -l)"
echo "Test files:   $(find "$PUBLIC_DIR/tests" -name '*.cpp' -o -name '*.c' | wc -l)"
echo "Tool files:   $(find "$PUBLIC_DIR/tools" -name '*.c' -o -name '*.h' | wc -l)"
echo "Patches:      $(find "$PUBLIC_DIR/patches" -name '*.patch' | wc -l)"
echo "Doc files:    $(find "$PUBLIC_DIR/docs" -name '*.md' -o -name '*.mdx' | wc -l)"
echo ""
echo "Preserved (public-only, untouched): README.md, README_CN.md, .gitignore, etc."
echo "NOT included: packaging/, .github/, integration-tests/, rpm/"
