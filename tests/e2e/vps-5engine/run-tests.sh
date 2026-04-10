#!/usr/bin/env bash
# =============================================================================
# run-tests.sh — Compare Apache vs Patched OLS (+LiteHTTPD) vs Stock OLS
#
# Usage: ./run-tests.sh <VPS_IP> [engines...]
#   engines: apache ols-patched ols-stock (default: all available)
# =============================================================================
set -uo pipefail

VPS_IP="${1:?Usage: $0 <VPS_IP>}"
shift
ENGINES=("${@:-apache ols-patched ols-stock}")
if [ ${#ENGINES[@]} -eq 1 ] && [ "${ENGINES[0]}" = "apache ols-patched ols-stock" ]; then
    ENGINES=(apache ols-patched ols-stock)
fi

declare -A PORTS=([apache]=80 [ols-patched]=8088 [ols-stock]=8089 [lsws]=8090 [cyberpanel]=8091)

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; YELLOW='\033[0;33m'; NC='\033[0m'
PASS=0; FAIL=0; SKIP=0
RESULTS_DIR="/tmp/vps-test-results-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RESULTS_DIR/headers"

# ── Helpers ──
curl_get() {
    local url="$1"
    curl -sS -o /dev/null -w '%{http_code}' --max-time 10 "$url" 2>/dev/null || echo "000"
}

curl_headers() {
    local url="$1"
    curl -sS -D - -o /dev/null --max-time 10 "$url" 2>/dev/null || echo ""
}

check_status() {
    local test_id="$1" desc="$2" path="$3" expected="$4"
    printf "%-12s %-40s" "$test_id" "$desc"

    local all_match=1 results=""
    for eng in "${ENGINES[@]}"; do
        local port="${PORTS[$eng]}"
        local code
        code=$(curl_get "http://${VPS_IP}:${port}${path}")
        results+="  ${code}"
        printf "  %-8s" "$code"
        if [ "$code" != "$expected" ] && [ "$eng" = "ols-patched" ]; then
            all_match=0
        fi
    done

    if [ $all_match -eq 1 ]; then
        echo -e "  ${GREEN}PASS${NC}"
        PASS=$((PASS+1))
    else
        echo -e "  ${RED}DIFF${NC}"
        FAIL=$((FAIL+1))
    fi
    echo "$test_id|$desc|$path|$expected|$results" >> "$RESULTS_DIR/raw.csv"
}

check_header() {
    local test_id="$1" desc="$2" path="$3" header="$4"
    printf "%-12s %-40s" "$test_id" "$desc"

    local apache_val="" ols_val=""
    for eng in "${ENGINES[@]}"; do
        local port="${PORTS[$eng]}"
        local hdrs val
        hdrs=$(curl_headers "http://${VPS_IP}:${port}${path}")
        val=$(echo "$hdrs" | grep -i "^${header}:" | head -1 | sed 's/^[^:]*: //' | tr -d '\r')
        printf "  %-8s" "${val:-(none)}"
        [ "$eng" = "apache" ] && apache_val="$val"
        [ "$eng" = "ols-patched" ] && ols_val="$val"
        echo "$hdrs" > "$RESULTS_DIR/headers/${test_id}_${eng}.txt"
    done

    if [ "$apache_val" = "$ols_val" ]; then
        echo -e "  ${GREEN}MATCH${NC}"
        PASS=$((PASS+1))
    else
        echo -e "  ${YELLOW}DIFF${NC}"
        FAIL=$((FAIL+1))
    fi
}

# ── Header ──
echo ""
echo "============================================================"
echo "  VPS 5-Engine Comparison Test"
echo "  IP: ${VPS_IP}"
echo "  Engines: ${ENGINES[*]}"
echo "  Results: ${RESULTS_DIR}"
echo "============================================================"
printf "%-12s %-40s" "ID" "Description"
for eng in "${ENGINES[@]}"; do printf "  %-8s" "$eng"; done
echo ""
echo "------------------------------------------------------------------------"

# ── 1. Baseline Tests ──
echo -e "\n${CYAN}=== Baseline Tests ===${NC}"
check_status "BL-001" "Homepage" "/" "200"
check_status "BL-002" "wp-login.php" "/wp-login.php" "200"
check_status "BL-003" "wp-admin (redirect)" "/wp-admin/" "302"
check_status "BL-004" "wp-cron.php" "/wp-cron.php" "200"
check_status "BL-005" "REST API" "/wp-json/wp/v2/posts" "200"
check_status "BL-006" "xmlrpc.php" "/xmlrpc.php" "405"
check_status "BL-007" "404 page" "/nonexistent-page-xyz" "404"
check_status "BL-008" "Static file (style.css)" "/wp-includes/css/dist/block-library/style.min.css" "200"

# ── 2. Security Tests (AIOS/Wordfence) ──
echo -e "\n${CYAN}=== Security Tests ===${NC}"
check_status "SEC-001" ".htaccess direct access" "/.htaccess" "403"
check_status "SEC-002" "wp-config.php access" "/wp-config.php" "403"
check_status "SEC-003" ".htpasswd access" "/.htpasswd" "403"
check_status "SEC-004" "readme.html access" "/readme.html" "200"
check_status "SEC-005" "PHP in uploads" "/wp-content/uploads/test.php" "403"
check_status "SEC-006" "Directory listing" "/wp-content/uploads/" "403"
check_status "SEC-007" "wp-includes PHP exec" "/wp-includes/test.php" "403"

# ── 3. Rewrite Tests ──
echo -e "\n${CYAN}=== Rewrite Tests ===${NC}"
check_status "RW-001" "Pretty permalink" "/sample-page/" "404"
check_status "RW-002" "Category page" "/category/uncategorized/" "200"
check_status "RW-003" "Feed" "/feed/" "200"
check_status "RW-004" "Sitemap (Yoast)" "/sitemap_index.xml" "200"

# ── 4. Cache Header Tests ──
echo -e "\n${CYAN}=== Cache Header Tests ===${NC}"
check_header "CH-001" "CSS Cache-Control" "/wp-includes/css/dist/block-library/style.min.css" "Cache-Control"
check_header "CH-002" "JS Cache-Control" "/wp-includes/js/jquery/jquery.min.js" "Cache-Control"
check_header "CH-003" "Image Cache-Control" "/wp-includes/images/w-logo-blue-white-bg.png" "Cache-Control"
check_header "CH-004" "CSS Expires" "/wp-includes/css/dist/block-library/style.min.css" "Expires"
check_header "CH-005" "X-Frame-Options" "/" "X-Frame-Options"

# ── 5. .htaccess Content Check ──
echo -e "\n${CYAN}=== .htaccess Content ===${NC}"
SSH="ssh -o StrictHostKeyChecking=no root@${VPS_IP}"
for eng in apache ols-patched; do
    case "$eng" in
        apache) docroot="/var/www/html" ;;
        ols-patched) docroot="/var/www/patched-ols/html" ;;
        ols-stock) docroot="/var/www/stock-ols/html" ;;
    esac
    lines=$($SSH "wc -l < ${docroot}/.htaccess 2>/dev/null" 2>/dev/null || echo "0")
    echo "  ${eng}: .htaccess has ${lines} lines"
    $SSH "cat ${docroot}/.htaccess" > "$RESULTS_DIR/htaccess_${eng}.txt" 2>/dev/null || true
done

# If both exist, diff them
if [ -f "$RESULTS_DIR/htaccess_apache.txt" ] && [ -f "$RESULTS_DIR/htaccess_ols-patched.txt" ]; then
    echo ""
    diff_output=$(diff "$RESULTS_DIR/htaccess_apache.txt" "$RESULTS_DIR/htaccess_ols-patched.txt" || true)
    if [ -z "$diff_output" ]; then
        echo -e "  ${GREEN}.htaccess files are identical${NC}"
    else
        echo -e "  ${YELLOW}.htaccess files differ:${NC}"
        echo "$diff_output" | head -20
    fi
fi

# ── 6. Attack Simulation ──
echo -e "\n${CYAN}=== Attack Simulation ===${NC}"
check_status "ATK-001" "SQL injection URL" "/?id=1%27%20OR%201=1--" "200"
check_status "ATK-002" "Path traversal" "/../../etc/passwd" "400"
check_status "ATK-003" "Null byte" "/index.php%00.txt" "400"
check_status "ATK-004" "Double encoding" "/%252e%252e/etc/passwd" "404"
check_status "ATK-005" "PHP upload in uploads" "/wp-content/uploads/evil.php" "403"

# ── Summary ──
TOTAL=$((PASS + FAIL + SKIP))
echo ""
echo "============================================================"
echo -e "  ${GREEN}PASS: ${PASS}${NC}  ${RED}DIFF: ${FAIL}${NC}  SKIP: ${SKIP}  TOTAL: ${TOTAL}"
echo "  Results saved to: ${RESULTS_DIR}"
echo "============================================================"

# Save summary
cat > "$RESULTS_DIR/summary.md" << EOF
# VPS Test Results — $(date)

- VPS: ${VPS_IP}
- Engines: ${ENGINES[*]}
- Pass: ${PASS} / ${TOTAL}
- Diff: ${FAIL} / ${TOTAL}
- Skip: ${SKIP} / ${TOTAL}
EOF

echo ""
echo "Done. Review $RESULTS_DIR for full details."
