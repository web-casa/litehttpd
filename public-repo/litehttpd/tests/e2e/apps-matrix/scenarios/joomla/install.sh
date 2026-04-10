#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
[joomla] install contract
- provision fixed Joomla version
- enable SEF URL mode with standard htaccess-enabled routing
- place shared _probe/*.php where safe for routing inspection
- capture final effective .htaccess to artifacts
- seed deterministic protected path fixtures if needed
EOF
