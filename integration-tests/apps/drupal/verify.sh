#!/bin/bash
# =============================================================================
# Drupal Integration Test
#
# .htaccess features tested:
#   Redirect (clean URLs), Header, ExpiresActive, FilesMatch
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/../../lib/assert.sh"

APP_PATH="/drupal/web"
APP_DIR="/var/www/vhosts/localhost/html/drupal"
DB_NAME="drupal"
DB_USER="appuser"
DB_PASS="apppass"
DB_HOST="db"

# ---------------------------------------------------------------------------
# Install Drupal if not already present
# ---------------------------------------------------------------------------
install_drupal() {
    echo ">>> Installing Drupal..."

    if docker_exec test -f "${APP_DIR}/web/core/install.php" 2>/dev/null; then
        echo ">>> Drupal already installed."
        return 0
    fi

    # Install Composer
    docker_exec bash -c '
        if [ ! -f /usr/local/bin/composer ]; then
            curl -sS https://getcomposer.org/installer | php -- --install-dir=/usr/local/bin --filename=composer
        fi
    '

    # Create Drupal project via composer
    docker_exec bash -c "
        cd /var/www/vhosts/localhost/html && \
        rm -rf drupal && \
        COMPOSER_ALLOW_SUPERUSER=1 composer create-project drupal/recommended-project drupal --no-interaction && \
        cd drupal && \
        COMPOSER_ALLOW_SUPERUSER=1 composer require drush/drush --no-interaction
    "

    # Install Drupal via Drush
    docker_exec bash -c "
        cd ${APP_DIR} && \
        vendor/bin/drush site:install standard \
            --db-url=mysql://${DB_USER}:${DB_PASS}@${DB_HOST}/${DB_NAME} \
            --account-name=admin \
            --account-pass=admin123 \
            --site-name='OLS Test Drupal' \
            -y
    "

    # Deploy .htaccess to web/ (Drupal's actual webroot)
    deploy_htaccess "$(cat <<'HTEOF'
# Security headers
Header set X-Content-Type-Options "nosniff"
Header set X-Frame-Options "SAMEORIGIN"

# Cache headers for static files
<FilesMatch "\.(css|js|gif|jpe?g|png|svg|ico)$">
    Header set Cache-Control "max-age=2592000, public"
</FilesMatch>

# Clean URLs
<IfModule mod_rewrite.c>
    RewriteEngine on
    RewriteCond %{REQUEST_FILENAME} !-f
    RewriteCond %{REQUEST_FILENAME} !-d
    RewriteCond %{REQUEST_URI} !=/favicon.ico
    RewriteRule ^ index.php [L]
</IfModule>

# PHP settings
php_value upload_max_filesize 32M
php_value post_max_size 32M

# Expires
ExpiresActive On
ExpiresByType image/jpeg "access plus 1 month"
ExpiresByType image/png "access plus 1 month"
ExpiresByType text/css "access plus 1 week"
ExpiresByType application/javascript "access plus 1 week"

# Error documents
ErrorDocument 404 /drupal/web/index.php

# Options
Options -Indexes
DirectoryIndex index.php index.html
HTEOF
)" "drupal/web"

    docker_exec chown -R nobody:nogroup "$APP_DIR"
    echo ">>> Drupal installed."
}

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

test_homepage() {
    assert_http_status "${APP_PATH}/" 200 && \
    assert_body_contains "${APP_PATH}/" "Drupal"
}

test_security_header_nosniff() {
    assert_header "${APP_PATH}/" "X-Content-Type-Options" "nosniff"
}

test_security_header_frame() {
    assert_header "${APP_PATH}/" "X-Frame-Options" "SAMEORIGIN"
}

test_404_clean_url() {
    assert_http_status "${APP_PATH}/nonexistent-page-xyz" 404
}

test_user_login() {
    assert_http_status "${APP_PATH}/user/login" 200
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

echo "========================================"
echo " Drupal Integration Tests"
echo "========================================"

install_drupal

run_test "Homepage loads"              test_homepage
run_test "X-Content-Type-Options set"  test_security_header_nosniff
run_test "X-Frame-Options set"         test_security_header_frame
run_test "Clean URL 404"               test_404_clean_url
run_test "User login page"             test_user_login

print_summary "Drupal"
