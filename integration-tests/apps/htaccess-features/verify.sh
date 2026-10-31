#!/bin/bash
# =============================================================================
# htaccess-features Integration Test
#
# Tests OLS-extra specific features that don't require a full CMS:
#   - Options -Indexes returning 403 (patch 0004)
#   - readApacheConf directive (patch 0003) — verifies binary has the code
#   - Various .htaccess directives on static content
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=integration-tests/lib/assert.sh
source "${SCRIPT_DIR}/../../lib/assert.sh"

APP_PATH="/htfeatures"
APP_DIR="/var/www/vhosts/localhost/html/htfeatures"

# ---------------------------------------------------------------------------
# Setup test files
# ---------------------------------------------------------------------------
setup_htfeatures() {
    echo ">>> Setting up htaccess-features test directory..."

    docker_exec bash -c "
        mkdir -p ${APP_DIR}/subdir
        mkdir -p ${APP_DIR}/noindex
        mkdir -p ${APP_DIR}/protected

        # Index page
        echo '<h1>htaccess-features OK</h1>' > ${APP_DIR}/index.html

        # Static file for header tests
        echo 'body { color: red; }' > ${APP_DIR}/style.css

        # PHP info page
        echo '<?php echo \"PHP_OK\"; ?>' > ${APP_DIR}/info.php

        # Subdir with files but no index (for -Indexes test)
        echo 'file1' > ${APP_DIR}/noindex/file1.txt
        echo 'file2' > ${APP_DIR}/noindex/file2.txt

        # Protected directory
        echo 'secret' > ${APP_DIR}/protected/secret.txt

        chown -R nobody:nogroup ${APP_DIR}
    "

    # Deploy .htaccess with Options -Indexes
    deploy_htaccess "$(cat <<'HTEOF'
# Test Options -Indexes (patch 0004: should return 403)
Options -Indexes

# Security headers
Header set X-Test-Header "htfeatures-ok"
Header set X-Content-Type-Options "nosniff"

# Cache for CSS
<FilesMatch "\.css$">
    Header set Cache-Control "max-age=3600"
</FilesMatch>

# Block access to protected directory
<Files "secret.txt">
    Order allow,deny
    Deny from all
</Files>

# Custom error document
ErrorDocument 403 "Access Forbidden"

# Default charset
AddDefaultCharset UTF-8
HTEOF
)" "htfeatures"

    # Separate .htaccess for noindex subdir
    deploy_htaccess "$(cat <<'HTEOF'
Options -Indexes
HTEOF
)" "htfeatures/noindex"

    echo ">>> htaccess-features setup complete."
}

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

test_index_page() {
    assert_http_status "${APP_PATH}/" 200 &&
    assert_body_contains "${APP_PATH}/" "htaccess-features OK"
}

test_custom_header() {
    assert_header "${APP_PATH}/" "X-Test-Header" "htfeatures-ok"
}

test_nosniff_header() {
    assert_header "${APP_PATH}/" "X-Content-Type-Options" "nosniff"
}

test_css_cache_header() {
    assert_header "${APP_PATH}/style.css" "Cache-Control" "max-age=3600"
}

test_options_no_indexes() {
    # Directory listing should be blocked with 403 (patch 0004)
    assert_http_status "${APP_PATH}/noindex/" 403
}

test_options_no_indexes_subdir() {
    # Specific file in the directory should still be accessible
    assert_http_status "${APP_PATH}/noindex/file1.txt" 200
}

test_protected_file() {
    assert_http_status "${APP_PATH}/protected/secret.txt" 403
}

test_php_works() {
    assert_http_status "${APP_PATH}/info.php" 200 &&
    assert_body_contains "${APP_PATH}/info.php" "PHP_OK"
}

test_readapacheconf_in_binary() {
    # Verify the patched OLS binary contains the readApacheConf code
    # by checking for the string in the binary
    docker_exec grep -c "readApacheConf" /usr/local/lsws/bin/openlitespeed >/dev/null 2>&1||
    docker_exec strings /usr/local/lsws/bin/openlitespeed 2>/dev/null | grep -q "readApacheConf"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

echo "========================================"
echo " htaccess-features Integration Tests"
echo "========================================"

setup_htfeatures

run_test "Index page loads"                    test_index_page
run_test "Custom X-Test-Header set"            test_custom_header
run_test "X-Content-Type-Options set"          test_nosniff_header
run_test "CSS Cache-Control header"            test_css_cache_header
run_test "Options -Indexes returns 403"        test_options_no_indexes
run_test "File in -Indexes dir accessible"     test_options_no_indexes_subdir
run_test "Protected file blocked"              test_protected_file
run_test "PHP execution works"                 test_php_works
run_test "readApacheConf in OLS binary"        test_readapacheconf_in_binary

print_summary "htaccess-features"
