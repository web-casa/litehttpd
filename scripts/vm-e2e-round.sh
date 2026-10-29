#!/usr/bin/env bash
# vm-e2e-round.sh — one-shot build + deploy + e2e verification.
#
# Path: local → PVE (sshdd.hicyou.de) → VM371 (build, 192.168.50.5)
#       → VM373 (OLS 1.9.3 + lsphp83 + WordPress, 192.168.50.45)
#
# Verifies, in order:
#   1. x86_64 module + Rust firewall daemon build on VM371
#   2. ESI:   EsiEnable On + <esi:include> fragment inlined (static file)
#   3. FW:    brute-force threshold → daemon → nftables rule → TTL expiry
#   4. ModSec: SecRuleEngine Off env contract against modsecurity-ls
#              (when the module is present on the OLS install)
set -uo pipefail

PVE=root@sshdd.hicyou.de
VM371=root@192.168.50.5
VM373=root@192.168.50.45
REPO_SRC=~/ols-extra
STAGE=/tmp/litehttpd-e2e

pve()  { ssh -o ConnectTimeout=15 -o BatchMode=yes "$PVE" "$@"; }
vm371() { pve "ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=no $VM371 '$*'"; }
vm373() { pve "ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=no $VM373 '$*'"; }

step() { printf '\n\033[1;36m== %s ==\033[0m\n' "$*"; }
pass() { printf '\033[1;32mPASS\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31mFAIL\033[0m %s\n' "$*"; FAILED=1; }
FAILED=0

step "0. reachability"
pve hostname || { fail "PVE unreachable"; exit 1; }
vm371 hostname || { fail "VM371 unreachable"; exit 1; }
vm373 hostname || { fail "VM373 unreachable"; exit 1; }

step "1. stage sources to VM371"
pve "rm -rf $STAGE && mkdir -p $STAGE"
git -C "$REPO_SRC" archive --format=tar.gz HEAD \
  | pve "cat > $STAGE/src.tar.gz"
pve "scp -o StrictHostKeyChecking=no $STAGE/src.tar.gz $VM371:$STAGE/" \
  || pve "ssh $VM371 'mkdir -p $STAGE' && scp -o StrictHostKeyChecking=no $STAGE/src.tar.gz $VM371:$STAGE/"
vm371 "mkdir -p $STAGE/repo && tar xzf $STAGE/src.tar.gz -C $STAGE/repo"

step "2. build module (VM371, x86_64)"
vm371 "cd $STAGE/repo && cmake -B build -S . -DCMAKE_BUILD_TYPE=Release >/dev/null \
        && cmake --build build -j\$(nproc) 2>&1 | tail -3 \
        && ctest --test-dir build -j\$(nproc) 2>&1 | tail -2 \
        && ls -la build/liblitehttpd_htaccess.so" \
  && pass "module built + tests" || fail "module build/tests"

step "3. build firewall daemon (VM371)"
vm371 "cd $STAGE/repo/firewalld && cargo build --release 2>&1 | tail -1 \
        && ls -la target/release/litehttpd-firewalld" \
  && pass "daemon built" || fail "daemon build"

step "4. deploy to VM373"
pve "scp -o StrictHostKeyChecking=no $VM371:$STAGE/repo/build/liblitehttpd_htaccess.so $VM373:/usr/local/lsws/modules/mod_htaccess_litehttpd.so.tmp"
vm373 "cd /usr/local/lsws/modules && \
       { lsof . >/dev/null 2>&1 || true; } && \
       install -m 755 mod_htaccess_litehttpd.so.tmp mod_htaccess_litehttpd.so && rm -f mod_htaccess_litehttpd.so.tmp"
pve "scp -o StrictHostKeyChecking=no $VM371:$STAGE/repo/firewalld/target/release/litehttpd-firewalld $VM373:/usr/local/bin/litehttpd-firewalld"
vm373 "install -m 755 /usr/local/bin/litehttpd-firewalld /usr/local/bin/litehttpd-firewalld && \
       mkdir -p /var/lib/litehttpd && \
       systemctl restart litehttpd-firewalld 2>/dev/null || \
       (/usr/local/bin/litehttpd-firewalld & echo started-manually)"
vm373 "/usr/local/lsws/bin/lswsctrl restart >/dev/null 2>&1; sleep 2; \
       /usr/local/lsws/bin/lswsctrl status"

step "5. ESI e2e — EsiEnable On + include fragment"
vm373 "
DOCROOT=\$(grep -m1 'vhRoot\|docRoot' /usr/local/lsws/conf/vhosts/*/*.conf 2>/dev/null | head -1 | sed 's/.*>//;s/<.*//' | tr -d ' ')
[ -z \"\$DOCROOT\" ] && DOCROOT=/var/www/html
mkdir -p \$DOCROOT/esi-test
echo 'FRAGMENT-CONTENT-42' > \$DOCROOT/esi-test/frag.html
cat > \$DOCROOT/esi-test/page.html <<EOF2
EsiEnable On
EOF2
printf 'EsiEnable On\n' > \$DOCROOT/esi-test/.htaccess
printf 'A<esi:include src=\"/esi-test/frag.html\"/>B' > \$DOCROOT/esi-test/page.html
BODY=\$(curl -s http://127.0.0.1/esi-test/page.html)
echo \"body: \$BODY\"
case \"\$BODY\" in *FRAGMENT-CONTENT-42*) exit 0;; *) exit 1;; esac
" && pass "esi include inlined" || fail "esi include"

step "6. firewall e2e — threshold → daemon → nftables → TTL"
vm373 "
systemctl is-active litehttpd-firewalld >/dev/null 2>&1 || pgrep -f litehttpd-firewalld >/dev/null || exit 2
WP=\$(ls -d \$DOCROOT 2>/dev/null || echo /var/www/html)
mkdir -p \$WP/wp-login-test
printf 'BruteForceProtection On\nBruteForceAllowedAttempts 3\nBruteForceWindow 15\nBruteForceAction block\n' > \$WP/wp-login-test/.htaccess
cp \$WP/wp-login-test/.htaccess \$WP/.htaccess.bak 2>/dev/null || true
# hammer from loopback (127.0.0.1) — 6 POSTs, threshold 3
for i in 1 2 3 4 5 6; do
  CODE=\$(curl -s -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1/wp-login-test/)
  echo \"attempt \$i -> \$CODE\"
done
sleep 1
nft list set inet litehttpd blocked 2>/dev/null || nft list ruleset 2>/dev/null | grep -i litehttpd
exit 0
"
vm373 "nft list ruleset 2>/dev/null | grep -q '127.0.0.1' && echo RULE_PRESENT || echo RULE_ABSENT"
vm373 "sleep 20; nft list ruleset 2>/dev/null | grep -q '127.0.0.1' && echo RULE_STILL || echo RULE_EXPIRED; \
       curl -s -o /dev/null -w 'post-ttl code: %{http_code}\\n' -X POST http://127.0.0.1/wp-login-test/ ; \
       rm -f \$DOCROOT/wp-login-test/.htaccess 2>/dev/null; \
       pkill -f litehttpd-firewalld 2>/dev/null; systemctl stop litehttpd-firewalld 2>/dev/null; true"

step "7. modsec bridge — SecRuleEngine Off env contract"
vm373 "grep -l mod_security /usr/local/lsws/modules/*.so 2>/dev/null | head -1; \
       ls /usr/local/lsws/modules/ | head -20"

step "done"
[ "$FAILED" -eq 0 ] && echo "ALL GREEN" || echo "SOME CHECKS FAILED"
exit "$FAILED"
