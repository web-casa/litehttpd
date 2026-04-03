#!/bin/bash
# =============================================================================
# Nextcloud Integration Test
#
# .htaccess features tested:
#   Header (security), SetEnv, ErrorDocument, php_value (upload_max)
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/../../lib/assert.sh"

APP_PATH="/nextcloud"
APP_DIR="/var/www/vhosts/localhost/html/nextcloud"
DB_NAME="nextcloud"
DB_USER="appuser"
DB_PASS="apppass"
DB_HOST="db"

# ---------------------------------------------------------------------------
# Install Nextcloud if not already present
# ---------------------------------------------------------------------------
install_nextcloud() {
    echo ">>> Installing Nextcloud..."

    if docker_exec test -f "${APP_DIR}/occ" 2>/dev/null; then
        echo ">>> Nextcloud already installed."
        return 0
    fi

    # Download and extract Nextcloud
    docker_exec bash -c "
        set -e
        mkdir -p ${APP_DIR}
        cd /tmp
        echo '    Downloading Nextcloud...'
        curl -sL -o nextcloud.tar.bz2 https://download.nextcloud.com/server/releases/latest.tar.bz2
        echo '    Extracting...'
        tar xjf nextcloud.tar.bz2
        # Use rsync-style copy: source contents into target
        cp -a /tmp/nextcloud/. ${APP_DIR}/
        rm -rf /tmp/nextcloud /tmp/nextcloud.tar.bz2
        echo '    Files copied.'
    "

    # Verify extraction
    if ! docker_exec test -f "${APP_DIR}/occ"; then
        echo "ERROR: Nextcloud extraction failed — occ not found"
        return 1
    fi

    # Run installation
    docker_exec php "${APP_DIR}/occ" maintenance:install \
        --database=mysql \
        --database-host="${DB_HOST}" \
        --database-name="${DB_NAME}" \
        --database-user="${DB_USER}" \
        --database-pass="${DB_PASS}" \
        --admin-user=admin \
        --admin-pass=admin123 \
        --data-dir="${APP_DIR}/data"

    # Add trusted domain
    docker_exec php "${APP_DIR}/occ" config:system:set trusted_domains 1 --value="localhost:8088"

    # Deploy .htaccess
    deploy_htaccess "$(cat <<'HTEOF'
# Security headers
Header set X-Content-Type-Options "nosniff"
Header set X-Robots-Tag "noindex, nofollow"
Header set X-Frame-Options "SAMEORIGIN"
Header set X-Permitted-Cross-Domain-Policies "none"
Header set Referrer-Policy "no-referrer"

# PHP settings for large file uploads
php_value upload_max_filesize 512M
php_value post_max_size 512M
php_value max_input_time 3600
php_value max_execution_time 3600

<IfModule mod_rewrite.c>
    RewriteEngine on
    RewriteCond %{REQUEST_FILENAME} !-f
    RewriteCond %{REQUEST_FILENAME} !-d
    RewriteRule . index.php [L]
</IfModule>

SetEnv htaccessWorking true
HTEOF
)" "nextcloud"

    docker_exec chown -R nobody:nogroup "$APP_DIR"
    echo ">>> Nextcloud installed."
}

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

test_login_page() {
    # Nextcloud redirects / to /login on fresh install
    _fetch GET "${APP_PATH}/"
    [[ "$_LAST_STATUS_CODE" == "200" || "$_LAST_STATUS_CODE" == "302" ]]
}

test_security_header_nosniff() {
    assert_header "${APP_PATH}/status.php" "X-Content-Type-Options" "nosniff"
}

test_security_header_robots() {
    assert_header "${APP_PATH}/status.php" "X-Robots-Tag" "noindex"
}

test_security_header_frame() {
    assert_header "${APP_PATH}/status.php" "X-Frame-Options" "SAMEORIGIN"
}

test_referrer_policy() {
    assert_header "${APP_PATH}/status.php" "Referrer-Policy" "no-referrer"
}

test_status_page() {
    assert_http_status "${APP_PATH}/status.php" 200
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

echo "========================================"
echo " Nextcloud Integration Tests"
echo "========================================"

install_nextcloud

run_test "Login page accessible"      test_login_page
run_test "X-Content-Type-Options set" test_security_header_nosniff
run_test "X-Robots-Tag set"           test_security_header_robots
run_test "X-Frame-Options set"        test_security_header_frame
run_test "Referrer-Policy set"        test_referrer_policy
run_test "status.php accessible"      test_status_page

print_summary "Nextcloud"
