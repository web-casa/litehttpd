#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
[wordpress-redirection] install contract
- provision fixed WordPress version
- install fixed Redirection plugin version
- import redirect fixtures that generate .htaccess rules
- capture final generated .htaccess to artifacts
- place shared _probe/*.php under docroot
EOF
