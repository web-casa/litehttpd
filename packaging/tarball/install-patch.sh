#!/bin/sh
# install-patch.sh — overlay the PATCHED openlitespeed binary + litehttpd
# module onto an EXISTING OpenLiteSpeed install, enabling the full feature set
# (RewriteRule execution, php_value/php_flag, readApacheConf, Options -Indexes
# returns 403).
#
# IMPORTANT: the patched binary is built for a SPECIFIC OpenLiteSpeed version
# and must match the version already installed (the rest of the lsws tree —
# admin console, configs — is version-specific). The script refuses to run on
# a mismatched version. The original binary is backed up before replacement.
#
# Layout shipped in the tarball:
#   litehttpd-ols-patch-<ver>/
#     ├── openlitespeed            (patched binary)
#     ├── litehttpd_htaccess.so
#     ├── VERSION                  (the OLS version this patch targets)
#     └── install.sh   (this script)
#
# Usage:
#   sudo ./install.sh
#   sudo LSWS_HOME=/opt/lsws ./install.sh
#   sudo FORCE_VERSION=1 ./install.sh   # skip the version match check (risky)
set -eu

PREFIX="${LSWS_HOME:-/usr/local/lsws}"
HERE=$(cd "$(dirname "$0")" && pwd)
NEWBIN="$HERE/openlitespeed"
SO="$HERE/litehttpd_htaccess.so"
EXPECT_VER=$(cat "$HERE/VERSION" 2>/dev/null || echo "")

if [ "$(id -u)" != 0 ]; then
    echo "ERROR: run as root (sudo ./install.sh)" >&2
    exit 1
fi
if [ ! -f "$NEWBIN" ] || [ ! -f "$SO" ]; then
    echo "ERROR: tarball payload missing — re-extract the package." >&2
    exit 1
fi
BIN="$PREFIX/bin/openlitespeed"
if [ ! -x "$BIN" ]; then
    echo "ERROR: OpenLiteSpeed not found at $BIN." >&2
    echo "  Install the official OpenLiteSpeed $EXPECT_VER first, or set LSWS_HOME." >&2
    exit 1
fi

# ── Version match check ──
CUR=$(cat "$PREFIX/VERSION" 2>/dev/null || true)
if [ -z "$CUR" ]; then
    CUR=$("$BIN" -v 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)
fi
if [ -n "$EXPECT_VER" ] && [ "$CUR" != "$EXPECT_VER" ]; then
    echo "ERROR: installed OpenLiteSpeed version is '${CUR:-unknown}', but this" >&2
    echo "       patch package targets $EXPECT_VER." >&2
    echo "  The patched binary must match the installed version. Options:" >&2
    echo "   - install official OpenLiteSpeed $EXPECT_VER, then re-run, or" >&2
    echo "   - use the module-only overlay (litehttpd-module-*.tgz), or" >&2
    echo "   - use the full tarball (openlitespeed-litehttpd-*.tgz)." >&2
    echo "  (To override at your own risk: FORCE_VERSION=1 sudo ./install.sh)" >&2
    [ "${FORCE_VERSION:-0}" = "1" ] || exit 1
    echo "FORCE_VERSION=1 set — proceeding despite version mismatch."
fi

# ── Back up + replace the binary ──
BAK="$BIN.orig.$(date +%Y%m%d%H%M%S)"
cp -a "$BIN" "$BAK"
echo "Backed up original binary -> $BAK"
install -m 755 "$NEWBIN" "$BIN"

# ── Install + enable the module ──
install -m 755 "$SO" "$PREFIX/modules/litehttpd_htaccess.so"
CONF="$PREFIX/conf/httpd_config.conf"
if [ -f "$CONF" ] && ! grep -q '^module litehttpd_htaccess' "$CONF"; then
    cp -a "$CONF" "$CONF.litehttpd.bak" 2>/dev/null || true
    printf '\nmodule litehttpd_htaccess {\n  ls_enabled 1\n}\n' >> "$CONF"
fi

echo ""
echo "=== Patched binary + module installed onto $PREFIX ==="
echo "Restart OpenLiteSpeed: $PREFIX/bin/lswsctrl restart"
echo "Rollback: cp -a '$BAK' '$BIN' && $PREFIX/bin/lswsctrl restart"
echo "Full feature set enabled (RewriteRule, php_value/flag, readApacheConf)."
