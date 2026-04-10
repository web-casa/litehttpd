#!/bin/bash
# =============================================================================
# E2E Directive Tests for OLS .htaccess Module
#
# Tests individual .htaccess directives against a running OLS container.
# Each test deploys its own .htaccess, runs assertions, then cleans up.
#
# Usage: bash tests/e2e/test_directives.sh
# =============================================================================

set -euo pipefail

# Source the assertions library
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/lib/assertions.sh"

# =============================================================================
# Task 3.1 — Header Directive Tests (Requirements: 4.1, 4.2, 4.3, 4.4, 9.1)
# =============================================================================

# Feature: ols-e2e-ci, Property 1: Header 指令响应头正确性
# Validates: Requirements 4.1
test_header_set() {
    deploy_htaccess 'Header set X-Custom-Test "hello-world"'
    assert_header_value GET / X-Custom-Test "hello-world"
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Property 1: Header 指令响应头正确性
# Validates: Requirements 4.2
test_header_always_set() {
    deploy_htaccess 'Header always set X-Always-Test "always-value"'
    assert_header_value GET / X-Always-Test "always-value"
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Property 1: Header 指令响应头正确性
# Validates: Requirements 4.3
test_header_unset() {
    deploy_htaccess 'Header unset X-Powered-By'
    assert_header_absent GET / X-Powered-By
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Property 1: Header 指令响应头正确性
# Validates: Requirements 4.4
test_header_append() {
    deploy_htaccess 'Header append X-Append-Test "appended-value"'
    assert_header_value GET / X-Append-Test "appended-value"
    cleanup_htaccess
}

# =============================================================================
# Task 3.3 — Expires Directive Tests (Requirements: 4.5, 4.6)
# =============================================================================

# Feature: ols-e2e-ci, Property 2: Expires 指令缓存控制正确性
# Validates: Requirements 4.5
test_expires_by_type() {
    deploy_htaccess 'ExpiresActive On
ExpiresByType image/jpeg "access plus 1 year"'
    # 1 year = 365 * 24 * 3600 = 31536000 seconds
    assert_header_value GET /photo.jpg Cache-Control "max-age=31536000"
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Property 2: Expires 指令缓存控制正确性
# Validates: Requirements 4.6
test_expires_default() {
    deploy_htaccess 'ExpiresActive On
ExpiresDefault "access plus 1 month"'
    # 1 month = 30 * 24 * 3600 = 2592000 seconds
    assert_header_value GET / Cache-Control "max-age=2592000"
    cleanup_htaccess
}

# =============================================================================
# Task 4.1 — Redirect Directive Tests (Requirements: 5.1, 5.2)
# =============================================================================

# Feature: ols-e2e-ci, Property 3: Redirect 指令状态码与 Location 正确性
# Validates: Requirements 5.1
test_redirect_301() {
    deploy_htaccess 'Redirect 301 /old-page http://localhost:8088/index.html'
    assert_redirect GET /old-page 301 "http://localhost:8088/index.html"
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Property 3: Redirect 指令状态码与 Location 正确性
# Validates: Requirements 5.2
test_redirect_default() {
    deploy_htaccess 'Redirect /temp-page http://localhost:8088/index.html'
    assert_redirect GET /temp-page 302 "http://localhost:8088/index.html"
    cleanup_htaccess
}

# =============================================================================
# Task 4.3 — Access Control Directive Tests (Requirements: 5.3, 5.4, 5.5, 5.6)
# =============================================================================

# Feature: ols-e2e-ci, Property 5: ErrorDocument 指令状态码正确性
# Validates: Requirements 5.3
test_error_document() {
    deploy_htaccess 'ErrorDocument 404 "Not Found Custom"'
    assert_status_code GET /nonexistent-resource-xyz 404
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Property 4: 访问控制指令拒绝/允许正确性
# Validates: Requirements 5.4
test_acl_deny() {
    deploy_htaccess 'Order Allow,Deny
Deny from all'
    assert_status_code GET / 403
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Property 4: 访问控制指令拒绝/允许正确性
# Validates: Requirements 5.5
test_require_denied() {
    deploy_htaccess 'Require all denied'
    assert_status_code GET / 403
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Property 4: 访问控制指令拒绝/允许正确性
# Validates: Requirements 5.6
test_require_granted() {
    deploy_htaccess 'Require all granted'
    assert_status_code GET / 200
    cleanup_htaccess
}

# =============================================================================
# Task 5.1 — Container Directive Tests (Requirements: 6.1, 6.2, 6.3, 6.4)
# =============================================================================

# Feature: ols-e2e-ci, Property 6: IfModule 容器条件执行正确性
# Validates: Requirements 6.1
test_ifmodule() {
    deploy_htaccess '<IfModule litehttpd_htaccess>
Header set X-IfModule-Test "module-loaded"
</IfModule>'
    assert_header_value GET / X-IfModule-Test "module-loaded"
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Property 6: IfModule 容器条件执行正确性
# Validates: Requirements 6.2
test_ifmodule_negated() {
    deploy_htaccess '<IfModule !mod_nonexistent.c>
Header set X-IfModule-Neg "negated-ok"
</IfModule>'
    assert_header_value GET / X-IfModule-Neg "negated-ok"
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Property 7: Files/FilesMatch 容器匹配正确性
# Validates: Requirements 6.3
test_files_match() {
    deploy_htaccess '<FilesMatch "\.jpg$">
Header set X-FilesMatch-Test "jpg-matched"
</FilesMatch>'
    # .jpg file should have the header
    assert_header_value GET /photo.jpg X-FilesMatch-Test "jpg-matched"
    # .html file should NOT have the header
    assert_header_absent GET / X-FilesMatch-Test
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Property 7: Files/FilesMatch 容器匹配正确性
# Validates: Requirements 6.4
test_files() {
    deploy_htaccess '<Files "index.html">
Header set X-Files-Test "exact-match"
</Files>'
    assert_header_value GET /index.html X-Files-Test "exact-match"
    cleanup_htaccess
}

# =============================================================================
# Task 5.3 — Environment Variable & Combined Directive Tests (Requirements: 7.1, 7.2, 7.3)
# =============================================================================

# Feature: ols-e2e-ci, Property 8: SetEnv 环境变量传递正确性
# Validates: Requirements 7.1
test_setenv() {
    deploy_htaccess 'SetEnv MY_VAR my_value
Header set X-Env-Test "%{MY_VAR}e"'
    assert_header_value GET / X-Env-Test "my_value"
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Combined directive correctness
# Validates: Requirements 7.2
test_combined_wordpress() {
    deploy_htaccess 'Header set X-WP-Test "wp-active"
ExpiresActive On
ExpiresDefault "access plus 1 hour"
ErrorDocument 404 "WP Not Found"'
    # Verify Header is set
    assert_header_value GET / X-WP-Test "wp-active"
    # Verify Expires is active (1 hour = 3600 seconds)
    assert_header_value GET / Cache-Control "max-age=3600"
    # Verify ErrorDocument for missing resource
    assert_status_code GET /nonexistent-wp-page 404
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Combined directive correctness
# Validates: Requirements 7.3
test_combined_security() {
    deploy_htaccess 'Header always set X-Frame-Options "SAMEORIGIN"
Header always set X-Content-Type-Options "nosniff"
Header always set X-XSS-Protection "1; mode=block"'
    assert_header_value GET / X-Frame-Options "SAMEORIGIN"
    assert_header_value GET / X-Content-Type-Options "nosniff"
    assert_header_value GET / X-XSS-Protection "1; mode=block"
    cleanup_htaccess
}

# =============================================================================
# Task 5.5 — Options, Limit, DirectoryIndex Directive Tests
#             (Requirements: 8.1, 8.2, 11.1, 11.2, 12.1)
# =============================================================================

# Feature: ols-e2e-ci, Property 9: Options Indexes 目录列表控制正确性
# Validates: Requirements 8.1
test_options_no_indexes() {
    # Create an empty directory with only a non-index file
    docker exec "${OLS_CONTAINER}" mkdir -p "${OLS_DOCROOT}/emptydir"
    docker exec "${OLS_CONTAINER}" sh -c "echo 'not-an-index' > ${OLS_DOCROOT}/emptydir/readme.txt"
    docker exec "${OLS_CONTAINER}" chown -R nobody:nogroup "${OLS_DOCROOT}/emptydir"

    deploy_htaccess 'Options -Indexes' "emptydir"
    assert_status_code GET /emptydir/ 403
    cleanup_htaccess "emptydir"

    # Clean up the test directory
    docker exec "${OLS_CONTAINER}" rm -rf "${OLS_DOCROOT}/emptydir"
}

# Feature: ols-e2e-ci, Property 9: Options Indexes 目录列表控制正确性
# Validates: Requirements 8.2
test_options_indexes() {
    # Create an empty directory with only a non-index file
    docker exec "${OLS_CONTAINER}" mkdir -p "${OLS_DOCROOT}/emptydir"
    docker exec "${OLS_CONTAINER}" sh -c "echo 'not-an-index' > ${OLS_DOCROOT}/emptydir/readme.txt"
    docker exec "${OLS_CONTAINER}" chown -R nobody:nogroup "${OLS_DOCROOT}/emptydir"

    deploy_htaccess 'Options +Indexes' "emptydir"
    assert_status_code GET /emptydir/ 200
    assert_body_contains GET /emptydir/ "readme.txt"
    cleanup_htaccess "emptydir"

    # Clean up the test directory
    docker exec "${OLS_CONTAINER}" rm -rf "${OLS_DOCROOT}/emptydir"
}

# Feature: ols-e2e-ci, Property 10: Limit/LimitExcept HTTP 方法限制正确性
# Validates: Requirements 11.1
test_limit_post() {
    deploy_htaccess '<Limit POST>
Require all denied
</Limit>'
    assert_status_code POST / 403
    assert_status_code GET / 200
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Property 10: Limit/LimitExcept HTTP 方法限制正确性
# Validates: Requirements 11.2
test_limit_except_get() {
    deploy_htaccess '<LimitExcept GET>
Require all denied
</LimitExcept>'
    assert_status_code GET / 200
    assert_status_code POST / 403
    cleanup_htaccess
}

# Feature: ols-e2e-ci, Property 11: DirectoryIndex 默认文件正确性
# Validates: Requirements 12.1
test_directory_index() {
    deploy_htaccess 'DirectoryIndex custom.html' "subdir"
    assert_body_contains GET /subdir/ "custom"
    cleanup_htaccess "subdir"
}

# =============================================================================
# Main execution — run all tests and print summary
# =============================================================================

# --- Task 3.1: Header directive tests ---
run_test "Header set"        test_header_set
run_test "Header always set" test_header_always_set
run_test "Header unset"      test_header_unset
run_test "Header append"     test_header_append

# --- Task 3.3: Expires directive tests ---
run_test "Expires by type"   test_expires_by_type
run_test "Expires default"   test_expires_default

# --- Task 4.1: Redirect directive tests ---
run_test "Redirect 301"      test_redirect_301
run_test "Redirect default"  test_redirect_default

# --- Task 4.3: Access control directive tests ---
run_test "ErrorDocument 404"  test_error_document
run_test "ACL Deny from all"  test_acl_deny
run_test "Require all denied" test_require_denied
run_test "Require all granted" test_require_granted

# --- Task 5.1: Container directive tests ---
run_test "IfModule positive"       test_ifmodule
run_test "IfModule negated"        test_ifmodule_negated
run_test "FilesMatch"              test_files_match
run_test "Files exact match"       test_files

# --- Task 5.3: Environment variable & combined directive tests ---
run_test "SetEnv"                  test_setenv
run_test "Combined WordPress"      test_combined_wordpress
run_test "Combined security"       test_combined_security

# --- Task 5.5: Options, Limit, DirectoryIndex directive tests ---
run_test "Options -Indexes"        test_options_no_indexes
run_test "Options +Indexes"        test_options_indexes
run_test "Limit POST"              test_limit_post
run_test "LimitExcept GET"         test_limit_except_get
run_test "DirectoryIndex"          test_directory_index

# --- Print final summary ---
print_summary
