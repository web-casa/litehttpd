#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
[wordpress-litespeed-cache] install contract
- provision fixed WordPress version
- install fixed LiteSpeed Cache plugin version
- enable htaccess-writing features relevant to browser cache/vary checks
- create deterministic assets under /wp-content/cache-matrix/
- capture final generated .htaccess to artifacts
- place shared _probe/*.php under docroot
EOF
