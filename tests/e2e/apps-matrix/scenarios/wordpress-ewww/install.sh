#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
[wordpress-ewww] install contract
- provision fixed WordPress version
- install fixed EWWW plugin version
- enable relevant browser-cache/resource rules
- create deterministic image assets and known protected original paths
- capture final generated .htaccess to artifacts
- place shared _probe/*.php under docroot
EOF
