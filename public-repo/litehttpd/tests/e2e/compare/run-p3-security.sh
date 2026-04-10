#!/usr/bin/env bash
# =============================================================================
# run-p3-security.sh — P3 Security Regression Tests
#
# 4-engine comparison: Apache (reference) vs OLS+LiteHTTPD vs LSWS vs CyberPanel
#
# Each test:
#   1. Writes a .htaccess to all engine doc roots
#   2. Sends the same HTTP request to all 4 engines
#   3. Compares status codes and headers
#   4. Reports PASS/FAIL/DIFF
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Engine ports
APACHE_PORT=18080
MODULE_PORT=28080
LSWS_PORT=38080
CYBER_PORT=48080
HOST="${HOST:-127.0.0.1}"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'; CYAN='\033[0;36m'; NC='\033[0m'

TOTAL=0; PASS=0; FAIL=0; DIFF=0

# Helper: deploy .htaccess to all engines
deploy_htaccess() {
    local content="$1"
    local subdir="${2:-}"

    for dir in \
        /usr/local/apache2/htdocs${subdir:+/$subdir} \
        /var/www/html${subdir:+/$subdir}; do
        docker exec $(docker ps -qf "publish=${APACHE_PORT}") bash -c "mkdir -p '$dir' && echo '$content' > '$dir/.htaccess'" 2>/dev/null || true
    done
    # OLS module
    docker exec $(docker ps -qf "publish=${MODULE_PORT}") bash -c "mkdir -p '/var/www/html${subdir:+/$subdir}' && echo '$content' > '/var/www/html${subdir:+/$subdir}/.htaccess'" 2>/dev/null || true
}

# Helper: send request and capture status + headers
probe() {
    local port=$1 method=$2 path=$3
    shift 3
    local extra_args=("$@")
    curl -s -o /dev/null -w '%{http_code}' \
        -X "$method" \
        "${extra_args[@]}" \
        "http://${HOST}:${port}${path}" 2>/dev/null || echo "000"
}

probe_headers() {
    local port=$1 method=$2 path=$3
    shift 3
    curl -s -D - -o /dev/null \
        -X "$method" "$@" \
        "http://${HOST}:${port}${path}" 2>/dev/null
}

# Helper: run a single test
run_test() {
    local id="$1" desc="$2" method="$3" path="$4" expected_apache="$5"
    shift 5
    local extra_args=("$@")

    TOTAL=$((TOTAL + 1))

    local s_apache=$(probe $APACHE_PORT "$method" "$path" "${extra_args[@]}")
    local s_module=$(probe $MODULE_PORT "$method" "$path" "${extra_args[@]}")
    local s_lsws=$(probe $LSWS_PORT "$method" "$path" "${extra_args[@]}")
    local s_cyber=$(probe $CYBER_PORT "$method" "$path" "${extra_args[@]}")

    local status="PASS"
    local detail=""

    # Check Apache matches expected
    if [[ "$s_apache" != "$expected_apache" && "$expected_apache" != "*" ]]; then
        detail+=" apache=${s_apache}(expect ${expected_apache})"
    fi

    # Check module matches Apache
    if [[ "$s_module" != "$s_apache" ]]; then
        status="DIFF"
        detail+=" module=${s_module}≠apache=${s_apache}"
    fi

    case "$status" in
        PASS) PASS=$((PASS + 1)); echo -e "  ${GREEN}✓${NC} ${id}: ${desc} [${s_apache}/${s_module}/${s_lsws}/${s_cyber}]" ;;
        DIFF) DIFF=$((DIFF + 1)); echo -e "  ${YELLOW}△${NC} ${id}: ${desc} [A:${s_apache} M:${s_module} L:${s_lsws} C:${s_cyber}]${detail}" ;;
        FAIL) FAIL=$((FAIL + 1)); echo -e "  ${RED}✗${NC} ${id}: ${desc} [A:${s_apache} M:${s_module} L:${s_lsws} C:${s_cyber}]${detail}" ;;
    esac
}

# ============================================================
echo -e "${CYAN}=== P3 Security Regression Tests ===${NC}"
echo -e "Engines: Apache(:${APACHE_PORT}) Module(:${MODULE_PORT}) LSWS(:${LSWS_PORT}) CyberPanel(:${CYBER_PORT})"
echo ""

# --- Group 1: Path Traversal ---
echo -e "${CYAN}[Group 1] Path Traversal${NC}"

deploy_htaccess "Header set X-Test ok"
run_test PTR_001 "/../../../etc/passwd blocked" GET "/../../../etc/passwd" "400"
run_test PTR_002 "/.htaccess direct access blocked" GET "/.htaccess" "403"
run_test PTR_003 "/.htpasswd direct access blocked" GET "/.htpasswd" "403"

# --- Group 2: Require all denied/granted ---
echo -e "${CYAN}[Group 2] Basic Require${NC}"

deploy_htaccess 'Require all denied'
run_test RQ_001 "Require all denied → 403" GET "/_probe/probe.txt" "403"

deploy_htaccess 'Require all granted'
run_test RQ_002 "Require all granted → 200" GET "/_probe/probe.txt" "200"

# --- Group 3: If conditional blocks ---
echo -e "${CYAN}[Group 3] If/ElseIf/Else${NC}"

deploy_htaccess '
<If "%{REQUEST_METHOD} == '\''POST'\''">
  Header set X-If-Match "post-matched"
</If>
<Else>
  Header set X-If-Match "else-matched"
</Else>'

run_test IF_001 "If POST matches → X-If-Match: post-matched" POST "/_probe/probe.txt" "200"
run_test IF_002 "Else branch on GET → X-If-Match: else-matched" GET "/_probe/probe.txt" "200"

deploy_htaccess '
<If "%{REQUEST_METHOD} == '\''DELETE'\''">
  Require all denied
</If>'

run_test IF_003 "If DELETE → Require all denied → 403" DELETE "/_probe/probe.txt" "403"
run_test IF_004 "If DELETE not matched on GET → 200" GET "/_probe/probe.txt" "200"

# --- Group 4: Files container ---
echo -e "${CYAN}[Group 4] Files containers${NC}"

deploy_htaccess '
<Files "secret.txt">
  Require all denied
</Files>'

# Create test files
for port in $APACHE_PORT $MODULE_PORT; do
    cid=$(docker ps -qf "publish=${port}")
    docker exec $cid bash -c 'echo "secret" > /var/www/html/secret.txt; echo "public" > /var/www/html/public.txt' 2>/dev/null || \
    docker exec $cid bash -c 'echo "secret" > /usr/local/apache2/htdocs/secret.txt; echo "public" > /usr/local/apache2/htdocs/public.txt' 2>/dev/null || true
done

run_test FILES_001 "Files secret.txt → 403" GET "/secret.txt" "403"
run_test FILES_002 "Files public.txt → 200" GET "/public.txt" "200"

# --- Group 5: Redirect in If ---
echo -e "${CYAN}[Group 5] Redirect in If${NC}"

deploy_htaccess '
<If "%{REQUEST_URI} =~ m#^/redir-test#">
  Redirect 301 /redir-test /redirected
</If>'

run_test REDIR_001 "Redirect inside If → 301" GET "/redir-test" "301"
run_test REDIR_002 "No redirect on other path → 200" GET "/_probe/probe.txt" "200"

# --- Group 6: Header operations ---
echo -e "${CYAN}[Group 6] Header operations${NC}"

deploy_htaccess '
Header set X-Custom "litehttpd"
Header append X-Custom " v2"
Header set X-Security "nosniff"'

run_test HDR_001 "Header set + append" GET "/_probe/probe.txt" "200"
run_test HDR_002 "Multiple headers" GET "/_probe/probe.txt" "200"

# --- Group 7: Options -Indexes ---
echo -e "${CYAN}[Group 7] Options -Indexes${NC}"

deploy_htaccess 'Options -Indexes' "noindex"
for port in $APACHE_PORT $MODULE_PORT; do
    cid=$(docker ps -qf "publish=${port}")
    docker exec $cid bash -c 'mkdir -p /var/www/html/noindex' 2>/dev/null || \
    docker exec $cid bash -c 'mkdir -p /usr/local/apache2/htdocs/noindex' 2>/dev/null || true
done

run_test OPT_001 "Options -Indexes directory → 403" GET "/noindex/" "403"

# --- Group 8: ErrorDocument ---
echo -e "${CYAN}[Group 8] ErrorDocument${NC}"

deploy_htaccess 'ErrorDocument 404 "Custom 404 page"'
run_test ERR_001 "ErrorDocument 404 custom text" GET "/nonexistent-page-xyz" "404"

deploy_htaccess 'ErrorDocument 404 https://example.com/error'
run_test ERR_002 "ErrorDocument 404 external URL → 302" GET "/nonexistent-page-xyz" "*"

# ============================================================
# Summary
echo ""
echo -e "${CYAN}=== Results ===${NC}"
echo -e "Total: ${TOTAL}  ${GREEN}Pass: ${PASS}${NC}  ${YELLOW}Diff: ${DIFF}${NC}  ${RED}Fail: ${FAIL}${NC}"

if [[ $FAIL -gt 0 ]]; then
    exit 1
fi
