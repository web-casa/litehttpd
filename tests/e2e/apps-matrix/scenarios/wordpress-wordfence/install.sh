#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
[wordpress-wordfence] install contract
- provision fixed WordPress version
- install fixed Wordfence plugin version
- enable hardening features that emit .htaccess deny/header rules
- provision deterministic protected paths used by deny cases
- capture final generated .htaccess to artifacts
- place shared _probe/*.php under docroot
EOF
