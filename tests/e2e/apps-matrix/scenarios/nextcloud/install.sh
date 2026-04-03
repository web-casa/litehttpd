#!/usr/bin/env bash
set -euo pipefail

cat <<'EOF'
[nextcloud] install contract
- provision fixed Nextcloud version
- perform deterministic install with fixed admin credentials
- keep shipped .htaccess semantics intact
- place shared _probe/*.php only where safe and non-conflicting
- capture final effective .htaccess to artifacts
EOF
