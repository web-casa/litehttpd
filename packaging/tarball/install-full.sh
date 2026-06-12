#!/bin/sh
# install-full.sh — install the LiteHTTPD-Full edition (patched OpenLiteSpeed
# + bundled litehttpd_htaccess module) from this binary tarball.
#
# Layout shipped in the tarball:
#   openlitespeed-litehttpd-<ver>/
#     ├── lsws/         (the complete patched install tree)
#     └── install.sh    (this script)
#
# Usage:
#   sudo ./install.sh            # fresh install to /usr/local/lsws
#   sudo FORCE=1 ./install.sh    # back up an existing install and reinstall
#   sudo LSWS_HOME=/opt/lsws ./install.sh
set -eu

PREFIX="${LSWS_HOME:-/usr/local/lsws}"
HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/lsws"

if [ "$(id -u)" != 0 ]; then
    echo "ERROR: run as root (sudo ./install.sh)" >&2
    exit 1
fi
if [ ! -d "$SRC" ]; then
    echo "ERROR: $SRC not found — is this the extracted tarball?" >&2
    exit 1
fi

# This package installs a COMPLETE patched OpenLiteSpeed. If one is already
# installed, prefer the overlay package (it preserves your config). Only
# overwrite when FORCE=1, and back up the existing tree first.
if [ -e "$PREFIX" ]; then
    if [ "${FORCE:-0}" != "1" ]; then
        echo "ERROR: $PREFIX already exists." >&2
        echo "  - To patch an existing OpenLiteSpeed in place, use the overlay" >&2
        echo "    package (litehttpd-ols-patch-*.tgz) — it keeps your config." >&2
        echo "  - To replace it with this full build, re-run with FORCE=1" >&2
        echo "    (the existing tree will be backed up)." >&2
        exit 1
    fi
    BAK="${PREFIX}.bak.$(date +%Y%m%d%H%M%S)"
    echo "FORCE=1: moving existing $PREFIX -> $BAK"
    mv "$PREFIX" "$BAK"
fi

# ── Service account (matches the RPM's %pre) ──
getent group lsadm >/dev/null 2>&1 || groupadd -r lsadm
getent passwd lsadm >/dev/null 2>&1 || \
    useradd -g lsadm -d / -r -s /sbin/nologin -c lsadm lsadm >/dev/null 2>&1 || true
getent group nogroup >/dev/null 2>&1 || groupadd -r nogroup || true

# ── Copy the install tree ──
mkdir -p "$PREFIX"
cp -a "$SRC/." "$PREFIX/"
echo TGZ > "$PREFIX/PLAT"

# ── Enable litehttpd module + rewrite + index.php (matches RPM %post) ──
CONF="$PREFIX/conf/httpd_config.conf"
if [ -f "$CONF" ] && ! grep -q '^module litehttpd_htaccess' "$CONF"; then
    printf '\nmodule litehttpd_htaccess {\n  ls_enabled 1\n}\n' >> "$CONF"
fi
VHCONF="$PREFIX/conf/vhosts/Example/vhconf.conf"
if [ -f "$VHCONF" ]; then
    sed -i '/^rewrite[[:space:]]*{/,/^}/s/enable 0/enable 1/' "$VHCONF" || true
    sed -i '/indexFiles/{ /index\.php/!s/indexFiles.*/indexFiles index.php, index.html/ }' "$VHCONF" || true
fi

chown -R lsadm:lsadm "$PREFIX" 2>/dev/null || true

# ── systemd unit (optional, only if systemd is present) ──
if [ -d /run/systemd/system ] && command -v systemctl >/dev/null 2>&1; then
    cat > /etc/systemd/system/lshttpd.service <<UNIT
[Unit]
Description=OpenLiteSpeed (LiteHTTPD)
After=network.target

[Service]
Type=forking
PIDFile=$PREFIX/logs/lshttpd.pid
ExecStart=$PREFIX/bin/lswsctrl start
ExecStop=$PREFIX/bin/lswsctrl stop
ExecReload=$PREFIX/bin/lswsctrl restart
Restart=on-failure

[Install]
WantedBy=multi-user.target
UNIT
    systemctl daemon-reload || true
    echo "systemd unit installed: systemctl enable --now lshttpd"
fi

echo ""
echo "=== LiteHTTPD-Full installed to $PREFIX ==="
echo "Start now : $PREFIX/bin/lswsctrl start"
echo "Admin     : set a password with $PREFIX/admin/misc/admpass.sh"
echo "Module    : litehttpd_htaccess enabled (RewriteRule + php_value execute)"
