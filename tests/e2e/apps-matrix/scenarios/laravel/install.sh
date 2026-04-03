#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
[laravel] install contract
- provision fixed Laravel version
- expose public/ as docroot
- place shared _probe/*.php under public/
- capture final .htaccess from public/.htaccess
- keep PHP and DB versions fixed across engines
EOF
