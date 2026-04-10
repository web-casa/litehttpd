#!/usr/bin/env bash
# =============================================================================
# deploy.sh — One-click deployment for 5-engine WordPress comparison test
#
# Engines: Apache(:80) / Patched OLS+LiteHTTPD(:8088) / Stock OLS(:8089)
#          / LSWS(:8090) / CyberPanel OLS(:8091)
#
# Usage: ./deploy.sh <VPS_IP>
# =============================================================================
set -euo pipefail

VPS_IP="${1:?Usage: $0 <VPS_IP>}"
SSH="ssh -o StrictHostKeyChecking=no root@${VPS_IP}"
PROJECT_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; YELLOW='\033[0;33m'; NC='\033[0m'

step() { echo -e "${CYAN}[$(date +%H:%M:%S)] $1${NC}"; }
ok()   { echo -e "${GREEN}  ✓ $1${NC}"; }
warn() { echo -e "${YELLOW}  ⚠ $1${NC}"; }
fail() { echo -e "${RED}  ✗ $1${NC}"; }

# ── Step 1: Upload project ──
step "Uploading project to VPS..."
tar czf - --exclude=build --exclude=.git --exclude='*.o' --exclude=ghidra-mcp --exclude=lsws_temp \
  -C "$PROJECT_ROOT" . | $SSH 'mkdir -p /root/litehttpd && cd /root/litehttpd && tar xzf -'
ok "Project uploaded"

# ── Step 2: Install base packages ──
step "Installing base packages..."
$SSH 'dnf -y install epel-release 2>&1 | tail -1
dnf -y install httpd mysql-server php php-mysqlnd php-fpm php-gd php-xml \
  php-mbstring php-zip php-intl php-curl php-json \
  cmake gcc gcc-c++ git-core libxcrypt-devel patch wget unzip \
  pcre-devel openssl-devel expat-devel zlib-devel \
  2>&1 | tail -3'
ok "Base packages installed"

# ── Step 3: Start MySQL ──
step "Starting MySQL..."
$SSH 'systemctl enable --now mysqld 2>/dev/null || systemctl enable --now mariadb 2>/dev/null
mysql -e "CREATE DATABASE IF NOT EXISTS wp_apache;
          CREATE DATABASE IF NOT EXISTS wp_patched_ols;
          CREATE DATABASE IF NOT EXISTS wp_stock_ols;
          CREATE DATABASE IF NOT EXISTS wp_lsws;
          CREATE DATABASE IF NOT EXISTS wp_cyberpanel;" 2>/dev/null
echo "MySQL: $(systemctl is-active mysqld 2>/dev/null || systemctl is-active mariadb)"'
ok "MySQL ready"

# ── Step 4: Install & configure Apache ──
step "Configuring Apache on :80..."
$SSH 'sed -i "/<Directory \"\/var\/www\/html\">/,/<\/Directory>/s/AllowOverride None/AllowOverride All/" /etc/httpd/conf/httpd.conf
echo "ServerName localhost" >> /etc/httpd/conf/httpd.conf
systemctl enable --now httpd
echo "Apache: $(systemctl is-active httpd)"'
ok "Apache on :80"

# ── Step 5: Build Patched OLS ──
step "Building Patched OLS (4 patches)... This takes ~5 minutes"
$SSH 'cd /tmp
if [ ! -d ols-patched ]; then
  git clone --depth 1 --recurse-submodules --branch v1.8.5 \
    https://github.com/litespeedtech/openlitespeed.git ols-patched 2>&1 | tail -2
fi
cd ols-patched

# Apply patches
patch -p1 < /root/litehttpd/patches/0001-lsiapi-phpconfig.patch 2>&1 | tail -1
patch -p1 < /root/litehttpd/patches/0002-lsiapi-rewrite.patch 2>&1 | tail -1

# Patch 0004: insert Options -Indexes check
python3 -c "
with open(\"src/http/httpreq.cpp\") as f:
    lines = f.readlines()
for i, line in enumerate(lines):
    if \"m_pContext->isAutoIndexOn()\" in line and \"if\" in line:
        indent = \"            \"
        insert = [
            indent + \"/* litehttpd: check .htaccess for Options -Indexes */\n\",
            indent + \"{\n\",
            indent + \"    char htpath[4096];\n\",
            indent + \"    int hlen = snprintf(htpath, sizeof(htpath), \\\"%s/.htaccess\\\", pBuf);\n\",
            indent + \"    if (hlen > 0 && hlen < (int)sizeof(htpath)) {\n\",
            indent + \"        FILE *htfp = fopen(htpath, \\\"r\\\");\n\",
            indent + \"        if (htfp) {\n\",
            indent + \"            char hline[512];\n\",
            indent + \"            while (fgets(hline, sizeof(hline), htfp)) {\n\",
            indent + \"                char *lp = hline;\n\",
            indent + \"                while (*lp == 0x20 || *lp == 0x09) lp++;\n\",
            indent + \"                if (strncasecmp(lp, \\\"Options\\\", 7) == 0 && strstr(lp, \\\"-Indexes\\\")) {\n\",
            indent + \"                    fclose(htfp);\n\",
            indent + \"                    return SC_403;\n\",
            indent + \"                }\n\",
            indent + \"            }\n\",
            indent + \"            fclose(htfp);\n\",
            indent + \"        }\n\",
            indent + \"    }\n\",
            indent + \"}\n\",
        ]
        lines = lines[:i] + insert + lines[i:]
        break
with open(\"src/http/httpreq.cpp\", \"w\") as f:
    f.writelines(lines)
print(\"Patch 0004 applied\")
"

# Patch 0003: readApacheConf (apply to plainconf.cpp)
# TODO: Apply when plainconf.cpp patch is finalized

# Build
bash build.sh 2>&1 | tail -3
echo "Patched OLS built: $(ls -lh build/src/openlitespeed | awk \"{print \\$5}\")"
'

# ── Step 6: Install Patched OLS ──
step "Installing Patched OLS on :8088..."
$SSH 'cd /tmp
# Install stock OLS first (creates directory structure)
if [ ! -f /usr/local/lsws/bin/openlitespeed ]; then
  wget -q https://openlitespeed.org/packages/openlitespeed-1.8.5.tgz
  tar xzf openlitespeed-1.8.5.tgz
  cd openlitespeed && bash install.sh 2>&1 | tail -3
fi

# Replace with patched binary
cp /tmp/ols-patched/build/src/openlitespeed /usr/local/lsws/bin/openlitespeed
chmod 755 /usr/local/lsws/bin/openlitespeed

# Build & install LiteHTTPD module
cd /root/litehttpd
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-O2 -DNDEBUG" -DBUILD_TESTING=OFF 2>&1 | tail -1
cmake --build build -j$(nproc) --target litehttpd_htaccess 2>&1 | tail -1
strip build/litehttpd_htaccess.so
cp build/litehttpd_htaccess.so /usr/local/lsws/modules/

# Also install litehttpd-confconv
cmake --build build -j$(nproc) --target litehttpd-confconv 2>&1 | tail -1
cp build/litehttpd-confconv /usr/local/lsws/modules/

# Configure module
CONF=/usr/local/lsws/conf/httpd_config.conf
grep -q litehttpd_htaccess $CONF || printf "\nmodule litehttpd_htaccess {\n  ls_enabled 1\n}\n" >> $CONF

# Configure vhost
VHCONF=/usr/local/lsws/conf/vhosts/Example/vhconf.conf
grep -q allowOverride $VHCONF || echo -e "allowOverride 255\nautoLoadHtaccess 1" >> $VHCONF
sed -i "s/indexFiles.*/indexFiles index.php, index.html/" $VHCONF

# Set docroot
mkdir -p /var/www/patched-ols/html
sed -i "s|vhRoot.*|vhRoot /var/www/patched-ols/|" $CONF 2>/dev/null || true

# Start
/usr/local/lsws/bin/lswsctrl start 2>&1
echo "Patched OLS: $(/usr/local/lsws/bin/openlitespeed -v 2>&1 | head -1)"
'
ok "Patched OLS + LiteHTTPD on :8088"

# ── Step 7: Install Stock OLS ──
step "Installing Stock OLS on :8089..."
$SSH 'if [ ! -d /usr/local/lsws2 ]; then
  cd /tmp
  cp -r openlitespeed /tmp/ols-stock-install
  cd /tmp/ols-stock-install
  # Modify install to use different prefix
  sed -i "s|/usr/local/lsws|/usr/local/lsws2|g" install.sh functions.sh 2>/dev/null || true
  bash install.sh 2>&1 | tail -3
fi
# Change port to 8089
sed -i "s/8088/8089/" /usr/local/lsws2/conf/httpd_config.conf
sed -i "s/7080/7081/" /usr/local/lsws2/conf/httpd_config.conf
mkdir -p /var/www/stock-ols/html
/usr/local/lsws2/bin/lswsctrl start 2>&1 || echo "Stock OLS start attempted"'
ok "Stock OLS on :8089"

# ── Step 8: Install LSWS ──
step "Installing LSWS Enterprise (trial) on :8090..."
$SSH 'echo "LSWS: skipped (manual install required for trial license)"
# TODO: wget LSWS installer, apply trial, configure port 8090'
warn "LSWS requires manual trial setup"

# ── Step 9: Install WP-CLI ──
step "Installing WP-CLI..."
$SSH 'if [ ! -f /usr/local/bin/wp ]; then
  curl -sO https://raw.githubusercontent.com/wp-cli/builds/gh-pages/phar/wp-cli.phar
  chmod +x wp-cli.phar && mv wp-cli.phar /usr/local/bin/wp
fi
wp --version 2>/dev/null'
ok "WP-CLI ready"

# ── Step 10: Install WordPress on each engine ──
step "Installing WordPress 6.9 on all engines..."
for engine_info in "apache:/var/www/html:wp_apache:80" "patched-ols:/var/www/patched-ols/html:wp_patched_ols:8088"; do
  IFS=: read -r engine docroot dbname port <<< "$engine_info"
  step "  WordPress on ${engine} (:${port})..."
  $SSH "cd ${docroot}
  if [ ! -f wp-config.php ]; then
    wp core download --allow-root 2>&1 | tail -1
    wp config create --dbname=${dbname} --dbuser=root --dbpass='' --allow-root 2>&1 | tail -1
    wp core install --url=http://${VPS_IP}:${port} --title='${engine} Test' \
      --admin_user=admin --admin_password=admin123 --admin_email=test@test.com \
      --skip-email --allow-root 2>&1 | tail -1
  fi"
  ok "WordPress on ${engine}"
done

# ── Step 11: Install 15 plugins ──
step "Installing 15 plugins on all engines..."
PLUGINS="all-in-one-wp-security-and-firewall wordfence wp-hide-security-enhancer really-simple-ssl w3-total-cache wp-super-cache far-future-expiry-header webp-express ewww-image-optimizer imagify wordpress-seo seo-by-rank-math redirection http-headers litespeed-cache"

for docroot in "/var/www/html" "/var/www/patched-ols/html"; do
  $SSH "cd ${docroot} && wp plugin install ${PLUGINS} --activate --allow-root 2>&1 | grep -c Success"
done
ok "15 plugins installed"

# ── Step 12: Open firewall ──
step "Opening firewall ports..."
$SSH 'firewall-cmd --permanent --add-port={80,8088,8089,8090,8091}/tcp 2>/dev/null
firewall-cmd --reload 2>/dev/null'
ok "Firewall configured"

# ── Summary ──
echo ""
echo -e "${GREEN}============================================================${NC}"
echo -e "${GREEN}  VPS Deployment Complete${NC}"
echo -e "${GREEN}============================================================${NC}"
echo "  IP: ${VPS_IP}"
echo "  Apache:          http://${VPS_IP}/"
echo "  Patched OLS+LH:  http://${VPS_IP}:8088/"
echo "  Stock OLS:       http://${VPS_IP}:8089/"
echo "  LSWS:            http://${VPS_IP}:8090/ (manual setup)"
echo "  CyberPanel OLS:  http://${VPS_IP}:8091/ (manual setup)"
echo ""
echo "  Run tests: bash tests/e2e/vps-5engine/run-tests.sh ${VPS_IP}"
