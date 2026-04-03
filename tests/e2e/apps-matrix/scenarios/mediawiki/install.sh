#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
[mediawiki] install contract
- provision fixed MediaWiki version
- enable pretty URL/front-controller routing
- place shared _probe/*.php where safe
- capture final effective .htaccess to artifacts
- seed deterministic routes and verify static asset paths exist
EOF
