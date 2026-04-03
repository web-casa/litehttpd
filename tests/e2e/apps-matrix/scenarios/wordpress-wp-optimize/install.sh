#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
[wordpress-wp-optimize] install contract
- provision fixed WordPress version
- install fixed WP-Optimize plugin version
- enable browser cache / expires features that write .htaccess rules
- create deterministic test assets and directories
- capture final generated .htaccess to artifacts
- place shared _probe/*.php under docroot
EOF
