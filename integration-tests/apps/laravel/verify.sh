#!/bin/bash
# =============================================================================
# Laravel Integration Test
#
# .htaccess features tested:
#   Redirect (routing), Header (CORS, security), SetEnv (APP_ENV)
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/../../lib/assert.sh"

APP_PATH="/laravel/public"
APP_DIR="/var/www/vhosts/localhost/html/laravel"
DB_NAME="laravel"
DB_USER="appuser"
DB_PASS="apppass"
DB_HOST="db"

# ---------------------------------------------------------------------------
# Install Laravel if not already present
# ---------------------------------------------------------------------------
install_laravel() {
    echo ">>> Installing Laravel..."

    # Check if already installed
    if docker_exec test -f "${APP_DIR}/artisan" 2>/dev/null; then
        echo ">>> Laravel already installed."
        return 0
    fi

    # Install Composer
    docker_exec bash -c '
        if [ ! -f /usr/local/bin/composer ]; then
            curl -sS https://getcomposer.org/installer | php -- --install-dir=/usr/local/bin --filename=composer
        fi
    '

    # Create Laravel project
    docker_exec bash -c "
        cd /var/www/vhosts/localhost/html && \
        rm -rf laravel && \
        COMPOSER_ALLOW_SUPERUSER=1 composer create-project --prefer-dist laravel/laravel laravel
    "

    # Configure .env — handle both commented and uncommented DB settings
    docker_exec bash -c "
        cd ${APP_DIR} && \
        php artisan key:generate && \
        sed -i 's|^.*DB_CONNECTION=.*|DB_CONNECTION=mysql|' .env && \
        sed -i 's|^.*DB_HOST=.*|DB_HOST=${DB_HOST}|' .env && \
        sed -i 's|^.*DB_PORT=.*|DB_PORT=3306|' .env && \
        sed -i 's|^.*DB_DATABASE=.*|DB_DATABASE=${DB_NAME}|' .env && \
        sed -i 's|^.*DB_USERNAME=.*|DB_USERNAME=${DB_USER}|' .env && \
        sed -i 's|^.*DB_PASSWORD=.*|DB_PASSWORD=${DB_PASS}|' .env && \
        sed -i 's|^APP_URL=.*|APP_URL=http://localhost:8088/laravel/public|' .env && \
        php artisan migrate --force 2>/dev/null || true
    "

    # Deploy .htaccess to laravel root (rewrite to public/)
    # NOTE: RewriteEngine/Rule/Cond handled by OLS native rewrite, not litehttpd
    deploy_htaccess "$(cat <<'HTEOF'
<IfModule mod_rewrite.c>
    RewriteEngine On
    RewriteRule ^(.*)$ public/$1 [L]
</IfModule>
HTEOF
)" "laravel"

    # Deploy .htaccess to laravel/public/
    deploy_htaccess "$(cat <<'HTEOF'
<IfModule mod_rewrite.c>
    RewriteEngine On
    RewriteCond %{REQUEST_FILENAME} !-d
    RewriteCond %{REQUEST_FILENAME} !-f
    RewriteRule ^ index.php [L]
</IfModule>

# Security headers
Header set X-Content-Type-Options "nosniff"
Header set X-XSS-Protection "1; mode=block"
Header set Referrer-Policy "strict-origin-when-cross-origin"

# CORS headers
Header set Access-Control-Allow-Origin "*"
Header set Access-Control-Allow-Methods "GET, POST, PUT, DELETE, OPTIONS"

# Protect sensitive files
<FilesMatch "\.(env|log)$">
    Order allow,deny
    Deny from all
</FilesMatch>

# PHP settings
php_value upload_max_filesize 32M
php_value post_max_size 32M
HTEOF
)" "laravel/public"

    docker_exec chown -R nobody:nogroup "$APP_DIR"
    echo ">>> Laravel installed."
}

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

test_homepage() {
    assert_http_status "${APP_PATH}/" 200 && \
    assert_body_contains "${APP_PATH}/" "Laravel"
}

test_security_header() {
    assert_header "${APP_PATH}/" "X-Content-Type-Options" "nosniff"
}

test_cors_header() {
    assert_header "${APP_PATH}/" "Access-Control-Allow-Origin" "*"
}

test_referrer_policy() {
    assert_header "${APP_PATH}/" "Referrer-Policy" "strict-origin-when-cross-origin"
}

test_env_file_protected() {
    # Create a .env file in public/ and verify FilesMatch blocks it
    docker_exec bash -c "echo 'SECRET=test' > ${APP_DIR}/public/.env"
    _fetch GET "${APP_PATH}/.env"
    [[ "$_LAST_STATUS_CODE" == "403" ]]
}

test_404_routing() {
    # Laravel returns its own 404 page via routing
    assert_http_status "${APP_PATH}/nonexistent-route-xyz" 404
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

echo "========================================"
echo " Laravel Integration Tests"
echo "========================================"

install_laravel

run_test "Homepage loads"             test_homepage
run_test "X-Content-Type-Options set" test_security_header
run_test "CORS header set"            test_cors_header
run_test "Referrer-Policy set"        test_referrer_policy
run_test ".env file protected"        test_env_file_protected
run_test "404 routed by Laravel"      test_404_routing

print_summary "Laravel"
