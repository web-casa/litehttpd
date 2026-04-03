#!/usr/bin/env bash
# =============================================================================
# deploy-test-vps.sh — Deploy 4-engine comparison test on Linode VPS
#
# Engines:
#   1. Apache httpd 2.4 (reference)
#   2. OpenLiteSpeed + LiteHTTPD module (our module)
#   3. LSWS Enterprise (trial)
#   4. CyberPanel OLS (custom OLS build)
#
# Usage:
#   ./deploy-test-vps.sh <VPS_IP>
# =============================================================================
set -euo pipefail

VPS_IP="${1:?Usage: $0 <VPS_IP>}"
SSH="ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10 root@${VPS_IP}"
SCP="scp -o StrictHostKeyChecking=no"
PROJECT_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; NC='\033[0m'

echo -e "${CYAN}[1/6] Installing Docker on VPS ...${NC}"
$SSH 'dnf -y install docker git && systemctl enable --now docker && docker --version' 2>&1 | tail -3

echo -e "${CYAN}[2/6] Uploading project to VPS ...${NC}"
rsync -azP --exclude=build --exclude=.git --exclude='*.o' \
  -e "ssh -o StrictHostKeyChecking=no" \
  "$PROJECT_ROOT/" "root@${VPS_IP}:/root/litehttpd/"

echo -e "${CYAN}[3/6] Building litehttpd_htaccess.so on VPS ...${NC}"
$SSH 'cd /root/litehttpd && dnf -y install cmake gcc gcc-c++ git-core libxcrypt-devel 2>&1 | tail -2 && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc) --target litehttpd_htaccess && strip build/litehttpd_htaccess.so && ls -lh build/litehttpd_htaccess.so'

echo -e "${CYAN}[4/6] Writing Docker Compose for 4-engine test ...${NC}"
$SSH 'cat > /root/litehttpd/docker-compose.4engine.yml << '"'"'YAML'"'"'
services:
  # Engine 1: Apache httpd (reference)
  apache:
    image: httpd:2.4
    volumes:
      - ./tests/e2e/compare/htdocs:/usr/local/apache2/htdocs
      - ./tests/e2e/compare/htdocs/.htaccess:/usr/local/apache2/htdocs/.htaccess
    ports:
      - "18080:80"
    command: >
      bash -c "
        sed -i '\''s/AllowOverride None/AllowOverride All/g'\'' /usr/local/apache2/conf/httpd.conf &&
        echo '\''LoadModule rewrite_module modules/mod_rewrite.so'\'' >> /usr/local/apache2/conf/httpd.conf 2>/dev/null || true &&
        echo '\''LoadModule expires_module modules/mod_expires.so'\'' >> /usr/local/apache2/conf/httpd.conf 2>/dev/null || true &&
        echo '\''LoadModule headers_module modules/mod_headers.so'\'' >> /usr/local/apache2/conf/httpd.conf 2>/dev/null || true &&
        httpd-foreground"

  # Engine 2: OLS + LiteHTTPD module
  ols-module:
    image: litespeedtech/openlitespeed:latest
    volumes:
      - ./tests/e2e/compare/htdocs:/var/www/html
      - ./build/litehttpd_htaccess.so:/usr/local/lsws/modules/litehttpd_htaccess.so
    ports:
      - "28080:8088"
    command: >
      bash -c "
        CONF=/usr/local/lsws/conf/httpd_config.conf &&
        grep -q litehttpd_htaccess $$CONF 2>/dev/null ||
        printf '\nmodule litehttpd_htaccess {\n  ls_enabled 1\n}\n' >> $$CONF &&
        /usr/local/lsws/bin/openlitespeed -d"
    healthcheck:
      test: ["CMD", "curl", "-sf", "http://localhost:8088/"]
      interval: 5s
      start_period: 10s

  # Engine 3: LSWS Enterprise (trial)
  lsws:
    image: litespeedtech/litespeed:latest
    volumes:
      - ./tests/e2e/compare/htdocs:/var/www/vhosts/localhost/html
    ports:
      - "38080:8088"
    healthcheck:
      test: ["CMD", "curl", "-sf", "http://localhost:8088/"]
      interval: 5s
      start_period: 30s

  # Engine 4: CyberPanel OLS (custom build with extra features)
  cyberpanel-ols:
    image: cyberpanel/cyberpanel:latest
    ports:
      - "48080:80"
    healthcheck:
      test: ["CMD", "curl", "-sf", "http://localhost/"]
      interval: 10s
      start_period: 60s
YAML'

echo -e "${CYAN}[5/6] Creating test htdocs with probe files ...${NC}"
$SSH 'mkdir -p /root/litehttpd/tests/e2e/compare/htdocs/_probe && cat > /root/litehttpd/tests/e2e/compare/htdocs/_probe/probe.php << '\''PHP'\''
<?php
header("X-Probe: ok");
echo json_encode([
  "engine" => php_sapi_name(),
  "server" => $_SERVER["SERVER_SOFTWARE"] ?? "unknown",
  "method" => $_SERVER["REQUEST_METHOD"],
  "uri"    => $_SERVER["REQUEST_URI"],
  "https"  => ($_SERVER["HTTPS"] ?? "off"),
]);
PHP
echo "OK" > /root/litehttpd/tests/e2e/compare/htdocs/_probe/probe.txt
echo "<?php phpinfo();" > /root/litehttpd/tests/e2e/compare/htdocs/_probe/info.php
'

echo -e "${CYAN}[6/6] Starting 4-engine Docker environment ...${NC}"
$SSH 'cd /root/litehttpd && docker compose -f docker-compose.4engine.yml up -d 2>&1 | tail -5'

echo ""
echo -e "${GREEN}=== VPS Ready ===${NC}"
echo "IP: ${VPS_IP}"
echo "Apache:         http://${VPS_IP}:18080/"
echo "OLS+LiteHTTPD:  http://${VPS_IP}:28080/"
echo "LSWS:           http://${VPS_IP}:38080/"
echo "CyberPanel OLS: http://${VPS_IP}:48080/"
echo ""
echo "Run tests: ssh root@${VPS_IP} 'cd /root/litehttpd && bash tests/e2e/compare/run-p3-security.sh'"
