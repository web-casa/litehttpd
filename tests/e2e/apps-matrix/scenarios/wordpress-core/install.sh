#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
[wordpress-core] install contract
- provision fixed WordPress version
- seed baseline database
- capture final generated .htaccess to artifacts
- place shared _probe/*.php under docroot
- do not mutate product code; app fixture only
EOF
