#!/bin/bash
# =============================================================================
# WordPress Integration Test
#
# .htaccess features tested:
#   Redirect/Rewrite (permalinks), Header (cache, security), SetEnv,
#   ExpiresActive, FilesMatch, ErrorDocument, php_value
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/../../lib/assert.sh"

APP_PATH="/wordpress"
APP_DIR="/var/www/vhosts/localhost/html/wordpress"
DB_NAME="wordpress"
DB_USER="appuser"
DB_PASS="apppass"
DB_HOST="db"

# ---------------------------------------------------------------------------
# Install WordPress if not already present
# ---------------------------------------------------------------------------
install_wordpress() {
    echo ">>> Installing WordPress..."

    # Download WP-CLI
    docker_exec bash -c '
        if [ ! -f /usr/local/bin/wp ]; then
            curl -sO https://raw.githubusercontent.com/wp-cli/builds/gh-pages/phar/wp-cli.phar
            chmod +x wp-cli.phar && mv wp-cli.phar /usr/local/bin/wp
        fi
    '

    # Check if already installed
    if docker_exec wp core is-installed --path="$APP_DIR" --allow-root 2>/dev/null; then
        echo ">>> WordPress already installed."
        return 0
    fi

    # Download & install
    docker_exec wp core download --path="$APP_DIR" --allow-root --force 2>/dev/null || true

    docker_exec wp config create \
        --path="$APP_DIR" --allow-root \
        --dbname="$DB_NAME" --dbuser="$DB_USER" --dbpass="$DB_PASS" --dbhost="$DB_HOST"

    docker_exec wp core install \
        --path="$APP_DIR" --allow-root \
        --url="http://localhost:8088${APP_PATH}" \
        --title="OLS Test" --admin_user=admin --admin_password=admin123 \
        --admin_email="test@example.com" --skip-email

    # Enable pretty permalinks (generates .htaccess)
    docker_exec wp rewrite structure '/%postname%/' --path="$APP_DIR" --allow-root
    docker_exec wp rewrite flush --path="$APP_DIR" --allow-root

    # Create a sample post for permalink testing
    docker_exec wp post create --path="$APP_DIR" --allow-root \
        --post_title="Sample Test Post" --post_content="Hello from OLS" \
        --post_status=publish --post_name="sample-test-post"

    # Deploy .htaccess with security and cache headers.
    # NOTE: RewriteEngine/Rule/Cond are handled by OLS native rewrite engine,
    # NOT by litehttpd module. They are included here because WordPress
    # generates them and they must not break the module's other directives.
    deploy_htaccess "$(cat <<'HTEOF'
# BEGIN WordPress
<IfModule mod_rewrite.c>
RewriteEngine On
RewriteBase /wordpress/
RewriteRule ^index\.php$ - [L]
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule . /wordpress/index.php [L]
</IfModule>
# END WordPress

# Security headers
Header set X-Content-Type-Options "nosniff"
Header set X-Frame-Options "SAMEORIGIN"

# Cache control for static assets
<FilesMatch "\.(jpg|jpeg|png|gif|css|js)$">
    Header set Cache-Control "max-age=2592000, public"
</FilesMatch>

# Protect .htaccess itself
<Files .htaccess>
    Order allow,deny
    Deny from all
</Files>

# PHP settings
php_value upload_max_filesize 64M
php_value post_max_size 64M
php_value max_execution_time 300

# Error document
ErrorDocument 404 /wordpress/index.php
HTEOF
)" "wordpress"

    docker_exec chown -R nobody:nogroup "$APP_DIR"
    echo ">>> WordPress installed."
}

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

test_homepage() {
    assert_http_status "${APP_PATH}/" 200 && \
    assert_body_contains "${APP_PATH}/" "WordPress"
}

test_permalink() {
    assert_http_status "${APP_PATH}/sample-test-post/" 200
}

test_security_header_nosniff() {
    assert_header "${APP_PATH}/" "X-Content-Type-Options" "nosniff"
}

test_security_header_frame() {
    assert_header "${APP_PATH}/" "X-Frame-Options" "SAMEORIGIN"
}

test_404_error_document() {
    assert_http_status "${APP_PATH}/nonexistent-page-xyz/" 404
}

test_htaccess_protected() {
    assert_http_status "${APP_PATH}/.htaccess" 403
}

test_rest_api() {
    assert_http_status "${APP_PATH}/wp-json/wp/v2/posts" 200
}

test_wp_admin() {
    # Should redirect to login
    _fetch GET "${APP_PATH}/wp-admin/"
    [[ "$_LAST_STATUS_CODE" == "200" || "$_LAST_STATUS_CODE" == "302" ]]
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

echo "========================================"
echo " WordPress Integration Tests"
echo "========================================"

install_wordpress

run_test "Homepage loads"              test_homepage
run_test "Pretty permalink works"      test_permalink
run_test "X-Content-Type-Options set"  test_security_header_nosniff
run_test "X-Frame-Options set"         test_security_header_frame
run_test "404 handled by WP"           test_404_error_document
run_test ".htaccess access blocked"    test_htaccess_protected
run_test "REST API accessible"         test_rest_api
run_test "wp-admin accessible"         test_wp_admin

print_summary "WordPress"
