#!/bin/bash
# rotate-botsecret.sh — atomically rotate the BotChallenge HMAC secret.
#
# Why: credential cookies are HMAC'd with a shared secret
# (/var/run/litehttpd/botchallenge.secret). Rotating it invalidates every
# outstanding cookie — visitors simply solve one more challenge — e.g.
# after suspected cookie harvesting or on a schedule.
#
# Workers cache the secret for their lifetime, so this script also
# gracefully restarts OLS (SIGUSR1: config reload, new workers pick up
# the new secret; in-flight requests finish on old workers).
#
# Usage:  sudo tools/rotate-botsecret.sh [--no-restart]
# Secret: /var/run/litehttpd/botchallenge.secret (override with
#         LITEHTTPD_BOT_SECRET, mirroring the module)
set -euo pipefail

LSWS="${LSWS_HOME:-/usr/local/lsws}"
SECRET="${LITEHTTPD_BOT_SECRET:-/var/run/litehttpd/botchallenge.secret}"
DIR="$(dirname "$SECRET")"
CONF="$LSWS/conf/httpd_config.conf"

[ "$(id -u)" -eq 0 ] || { echo "must run as root (needs chown)" >&2; exit 1; }
[ -f "$CONF" ] || { echo "config not found: $CONF" >&2; exit 1; }

# Worker user/group from server config (module hooks run in workers)
BCUSER=$(awk '$1=="user" {print $2; exit}' "$CONF"); BCUSER=${BCUSER:-nobody}
BCGROUP=$(awk '$1=="group"{print $2; exit}' "$CONF"); BCGROUP=${BCGROUP:-nogroup}
getent passwd "$BCUSER" >/dev/null || { echo "worker user '$BCUSER' missing" >&2; exit 1; }
getent group  "$BCGROUP" >/dev/null || BCGROUP=$(id -gn "$BCUSER")

install -d -m 0770 -o "$BCUSER" -g "$BCGROUP" "$DIR"

# Atomic swap: new bytes land in a sibling temp file, then rename(2).
# Readers always see either the old or the new complete secret.
TMP="$(mktemp "$DIR/.botsecret.XXXXXX")"
trap 'rm -f "$TMP"' EXIT
dd if=/dev/urandom of="$TMP" bs=32 count=1 status=none
chown "$BCUSER:$BCGROUP" "$TMP"
chmod 0600 "$TMP"
mv -f "$TMP" "$SECRET"
trap - EXIT
echo "rotated: $SECRET (owner $BCUSER:$BCGROUP, mode 0600)"

if [ "${1:-}" != "--no-restart" ] && [ -x "$LSWS/bin/lswsctrl" ]; then
    "$LSWS/bin/lswsctrl" restart
    echo "OLS restarted: new workers now sign with the new secret."
else
    echo "restart skipped — run '$LSWS/bin/lswsctrl restart' yourself."
fi
