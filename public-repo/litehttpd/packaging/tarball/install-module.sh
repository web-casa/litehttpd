#!/bin/sh
# install-module.sh — overlay the litehttpd_htaccess module onto an EXISTING
# OpenLiteSpeed install (official package or any build). Version-independent:
# the module uses only the public LSIAPI, so it works on any OLS >= 1.7.
#
# It does NOT replace the openlitespeed binary, so RewriteRule and php_value
# are parsed but NOT executed (those need the patched binary — use the
# litehttpd-ols-patch overlay or the full tarball for those features).
#
# Layout shipped in the tarball:
#   litehttpd-module-<ver>/
#     ├── litehttpd_htaccess.so
#     └── install.sh   (this script)
#
# Usage:
#   sudo ./install.sh
#   sudo LSWS_HOME=/opt/lsws ./install.sh
set -eu

PREFIX="${LSWS_HOME:-/usr/local/lsws}"
HERE=$(cd "$(dirname "$0")" && pwd)
SO="$HERE/litehttpd_htaccess.so"

if [ "$(id -u)" != 0 ]; then
    echo "ERROR: run as root (sudo ./install.sh)" >&2
    exit 1
fi
if [ ! -f "$SO" ]; then
    echo "ERROR: $SO not found — is this the extracted tarball?" >&2
    exit 1
fi
if [ ! -d "$PREFIX/modules" ] || [ ! -f "$PREFIX/conf/httpd_config.conf" ]; then
    echo "ERROR: OpenLiteSpeed not found at $PREFIX." >&2
    echo "  Install the official OpenLiteSpeed first, or set LSWS_HOME." >&2
    exit 1
fi

install -m 755 "$SO" "$PREFIX/modules/litehttpd_htaccess.so"

CONF="$PREFIX/conf/httpd_config.conf"
if ! grep -q '^module litehttpd_htaccess' "$CONF"; then
    cp -a "$CONF" "$CONF.litehttpd.bak" 2>/dev/null || true
    printf '\nmodule litehttpd_htaccess {\n  ls_enabled 1\n}\n' >> "$CONF"
    echo "Enabled module in $CONF (backup: $CONF.litehttpd.bak)"
else
    echo "Module already enabled in $CONF"
fi

echo ""
echo "=== litehttpd_htaccess module installed onto $PREFIX ==="
echo "Restart OpenLiteSpeed: $PREFIX/bin/lswsctrl restart"
echo "NOTE: RewriteRule / php_value are parsed but NOT executed with the stock"
echo "      binary. For those, use litehttpd-ols-patch-*.tgz or the full tarball."
