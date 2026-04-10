#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
[drupal] install contract
- provision fixed Drupal version
- enable clean URLs / standard shipped .htaccess
- place shared _probe/*.php where docroot allows probing
- capture final effective .htaccess to artifacts
- seed deterministic path fixtures used by deny/static tests
EOF
