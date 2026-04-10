#!/bin/bash
# Sync public-repo from litehttpd-private
# Usage: bash public-repo/sync.sh

set -e
PRIVATE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PUBLIC_DIR="$PRIVATE_DIR/public-repo/litehttpd"

echo "=== Syncing public repo from private ==="
echo "Private: $PRIVATE_DIR"
echo "Public:  $PUBLIC_DIR"

rm -rf "$PUBLIC_DIR"
mkdir -p "$PUBLIC_DIR"

# Source code
cp -r "$PRIVATE_DIR/src" "$PUBLIC_DIR/"
cp -r "$PRIVATE_DIR/include" "$PUBLIC_DIR/"
cp -r "$PRIVATE_DIR/tools" "$PUBLIC_DIR/"
cp "$PRIVATE_DIR/CMakeLists.txt" "$PUBLIC_DIR/"

# Patches
cp -r "$PRIVATE_DIR/patches" "$PUBLIC_DIR/"

# Tests
cp -r "$PRIVATE_DIR/tests" "$PUBLIC_DIR/"

# Docs
cp -r "$PRIVATE_DIR/docs" "$PUBLIC_DIR/"
    rm -f "$PUBLIC_DIR/docs/rpm-build-lessons.md"
# Remove node_modules if copied
rm -rf "$PUBLIC_DIR/docs/node_modules" "$PUBLIC_DIR/docs/dist"

# Top-level files
cp "$PRIVATE_DIR/LICENSE" "$PUBLIC_DIR/" 2>/dev/null || true
cp "$PRIVATE_DIR/CHANGELOG.md" "$PUBLIC_DIR/"

# Count
echo ""
echo "=== Synced ==="
echo "Source files: $(find $PUBLIC_DIR/src -name '*.c' -o -name '*.h' | wc -l)"
echo "Test files:   $(find $PUBLIC_DIR/tests -name '*.cpp' -o -name '*.c' | wc -l)"
echo "Tool files:   $(find $PUBLIC_DIR/tools -name '*.c' -o -name '*.h' | wc -l)"
echo "Patches:      $(ls $PUBLIC_DIR/patches/*.patch | wc -l)"
echo "Doc files:    $(find $PUBLIC_DIR/docs -name '*.md' -o -name '*.mdx' | wc -l)"
echo ""
echo "NOT included: packaging/, .github/, integration-tests/, rpm/"
