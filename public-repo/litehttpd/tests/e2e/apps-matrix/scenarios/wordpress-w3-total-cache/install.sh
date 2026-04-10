#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
[wordpress-w3-total-cache] install contract
- provision fixed WordPress version
- install fixed W3 Total Cache plugin version
- enable plugin features that emit .htaccess rules for cache/expires/vary tests
- create deterministic test assets under /wp-content/cache-matrix/
- capture final generated .htaccess to artifacts
- place shared _probe/*.php under docroot
EOF
