#!/bin/bash
# LiteHTTPD RPM Repository Setup
# Usage: curl -s https://rpms.litehttpd.com/setup.sh | bash

set -e

REPO_URL="https://rpms.litehttpd.com"
GPG_KEY_URL="$REPO_URL/RPM-GPG-KEY-litehttpd"

echo "=== LiteHTTPD RPM Repository Setup ==="

# Detect EL version
if [ -f /etc/os-release ]; then
    # shellcheck source=/dev/null
    . /etc/os-release
    MAJOR=$(echo "$VERSION_ID" | cut -d. -f1)
else
    echo "Error: Cannot detect OS version"
    exit 1
fi

ARCH=$(uname -m)

if [ "$MAJOR" != "8" ] && [ "$MAJOR" != "9" ] && [ "$MAJOR" != "10" ]; then
    echo "Error: Unsupported EL version: $MAJOR (supported: 8, 9, 10)"
    exit 1
fi

if [ "$ARCH" != "x86_64" ]; then
    echo "Error: Unsupported architecture: $ARCH (supported: x86_64)"
    exit 1
fi

echo "Detected: EL${MAJOR} ${ARCH}"

# Import GPG key
echo "Importing GPG key..."
rpm --import "$GPG_KEY_URL"

# Create repo file
cat > /etc/yum.repos.d/litehttpd.repo << EOF
[litehttpd]
name=LiteHTTPD - EL${MAJOR}
baseurl=${REPO_URL}/el${MAJOR}/${ARCH}/
gpgcheck=1
repo_gpgcheck=1
gpgkey=${GPG_KEY_URL}
enabled=1
EOF

echo "Repository configured: /etc/yum.repos.d/litehttpd.repo"
echo ""
echo "Install LiteHTTPD (Full mode - patched OLS + module):"
echo "  dnf install openlitespeed-litehttpd"
echo ""
echo "Done!"
