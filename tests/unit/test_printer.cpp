/**
 * test_printer.cpp - Unit tests for htaccess_printer
 *
 * Tests htaccess_print() output for directive types, FilesMatch
 * nesting, and round-trip compatibility with htaccess_parse().
 *
 * Validates: Requirements 2.5
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <string>

extern "C" {
#include "htaccess_directive.h"
#include "htaccess_printer.h"
#include "htaccess_parser.h"
}

/* ---- Helper: allocate a directive with strdup'd name/value ---- */

static htaccess_directive_t *make_dir(directive_type_t type,
                                      const char *name,
                                      const char *value,
                                      int line = 1) {
    auto *d = static_cast<htaccess_directive_t *>(
        calloc(1, sizeof(htaccess_directive_t)));
    d->type = type;
    d->line_number = line;
    if (name)  d->name  = strdup(name);
    if (value) d->value = strdup(value);
    d->next = nullptr;
    return d;
}

/* ---- NULL input ---- */

TEST(PrinterTest, NullHeadReturnsNull) {
    char *result = htaccess_print(nullptr);
    EXPECT_EQ(result, nullptr);
}

/* ---- Header directives ---- */

TEST(PrinterTest, HeaderSet) {
    auto *d = make_dir(DIR_HEADER_SET, "X-Frame-Options", "DENY");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "Header set X-Frame-Options DENY\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, HeaderUnset) {
    auto *d = make_dir(DIR_HEADER_UNSET, "Server", nullptr);
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "Header unset Server\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, HeaderAppend) {
    auto *d = make_dir(DIR_HEADER_APPEND, "Cache-Control", "no-cache");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "Header append Cache-Control no-cache\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, HeaderMerge) {
    auto *d = make_dir(DIR_HEADER_MERGE, "Cache-Control", "public");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "Header merge Cache-Control public\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, HeaderAdd) {
    auto *d = make_dir(DIR_HEADER_ADD, "Set-Cookie", "id=abc");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "Header add Set-Cookie id=abc\n");
    free(out);
    htaccess_directives_free(d);
}

/* ---- RequestHeader directives ---- */

TEST(PrinterTest, RequestHeaderSet) {
    auto *d = make_dir(DIR_REQUEST_HEADER_SET, "X-Forwarded-For", "1.2.3.4");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "RequestHeader set X-Forwarded-For 1.2.3.4\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, RequestHeaderUnset) {
    auto *d = make_dir(DIR_REQUEST_HEADER_UNSET, "Proxy", nullptr);
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "RequestHeader unset Proxy\n");
    free(out);
    htaccess_directives_free(d);
}

/* ---- PHP directives ---- */

TEST(PrinterTest, PhpValue) {
    auto *d = make_dir(DIR_PHP_VALUE, "upload_max_filesize", "64M");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "php_value upload_max_filesize 64M\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, PhpFlag) {
    auto *d = make_dir(DIR_PHP_FLAG, "display_errors", "on");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "php_flag display_errors on\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, PhpAdminValue) {
    auto *d = make_dir(DIR_PHP_ADMIN_VALUE, "memory_limit", "256M");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "php_admin_value memory_limit 256M\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, PhpAdminFlag) {
    auto *d = make_dir(DIR_PHP_ADMIN_FLAG, "log_errors", "off");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "php_admin_flag log_errors off\n");
    free(out);
    htaccess_directives_free(d);
}

/* ---- Access control directives ---- */

TEST(PrinterTest, OrderAllowDeny) {
    auto *d = make_dir(DIR_ORDER, nullptr, nullptr);
    d->data.acl.order = ORDER_ALLOW_DENY;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "Order Allow,Deny\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, OrderDenyAllow) {
    auto *d = make_dir(DIR_ORDER, nullptr, nullptr);
    d->data.acl.order = ORDER_DENY_ALLOW;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "Order Deny,Allow\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, AllowFrom) {
    auto *d = make_dir(DIR_ALLOW_FROM, nullptr, "192.168.1.0/24");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "Allow from 192.168.1.0/24\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, DenyFrom) {
    auto *d = make_dir(DIR_DENY_FROM, nullptr, "all");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "Deny from all\n");
    free(out);
    htaccess_directives_free(d);
}

/* ---- Redirect directives ---- */

TEST(PrinterTest, RedirectDefault302) {
    auto *d = make_dir(DIR_REDIRECT, "/old", "http://example.com/new");
    d->data.redirect.status_code = 302;
    d->data.redirect.pattern = nullptr;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "Redirect /old http://example.com/new\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, Redirect301) {
    auto *d = make_dir(DIR_REDIRECT, "/old", "http://example.com/new");
    d->data.redirect.status_code = 301;
    d->data.redirect.pattern = nullptr;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "Redirect permanent /old http://example.com/new\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, Redirect303Seeother) {
    auto *d = make_dir(DIR_REDIRECT, "/old", "http://example.com/new");
    d->data.redirect.status_code = 303;
    d->data.redirect.pattern = nullptr;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "Redirect seeother /old http://example.com/new\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, RedirectGoneNoTarget) {
    auto *d = make_dir(DIR_REDIRECT, "/removed", nullptr);
    d->data.redirect.status_code = 410;
    d->data.redirect.pattern = nullptr;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "Redirect gone /removed\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, Redirect307NumericPreserved) {
    auto *d = make_dir(DIR_REDIRECT, "/old", "http://example.com/new");
    d->data.redirect.status_code = 307;
    d->data.redirect.pattern = nullptr;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "Redirect 307 /old http://example.com/new\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, RedirectMatch302) {
    auto *d = make_dir(DIR_REDIRECT_MATCH, nullptr, "http://example.com/$1");
    d->data.redirect.status_code = 302;
    d->data.redirect.pattern = strdup("^/old/(.*)$");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "RedirectMatch ^/old/(.*)$ http://example.com/$1\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, RedirectMatch301) {
    auto *d = make_dir(DIR_REDIRECT_MATCH, nullptr, "http://example.com/$1");
    d->data.redirect.status_code = 301;
    d->data.redirect.pattern = strdup("^/old/(.*)$");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "RedirectMatch permanent ^/old/(.*)$ http://example.com/$1\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, RedirectMatchGone) {
    auto *d = make_dir(DIR_REDIRECT_MATCH, nullptr, nullptr);
    d->data.redirect.status_code = 410;
    d->data.redirect.pattern = strdup("^/removed/");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "RedirectMatch gone ^/removed/\n");
    free(out);
    htaccess_directives_free(d);
}

/* ---- ErrorDocument ---- */

TEST(PrinterTest, ErrorDocument) {
    auto *d = make_dir(DIR_ERROR_DOCUMENT, nullptr, "/errors/404.html");
    d->data.error_doc.error_code = 404;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "ErrorDocument 404 /errors/404.html\n");
    free(out);
    htaccess_directives_free(d);
}

/* ---- Expires directives ---- */

TEST(PrinterTest, ExpiresActiveOn) {
    auto *d = make_dir(DIR_EXPIRES_ACTIVE, nullptr, nullptr);
    d->data.expires.active = 1;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "ExpiresActive On\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, ExpiresActiveOff) {
    auto *d = make_dir(DIR_EXPIRES_ACTIVE, nullptr, nullptr);
    d->data.expires.active = 0;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "ExpiresActive Off\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, ExpiresByType) {
    auto *d = make_dir(DIR_EXPIRES_BY_TYPE, "image/png",
                       "access plus 1 month");
    d->data.expires.duration_sec = 2592000;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "ExpiresByType image/png \"access plus 1 month\"\n");
    free(out);
    htaccess_directives_free(d);
}

/* ---- Environment variable directives ---- */

TEST(PrinterTest, SetEnv) {
    auto *d = make_dir(DIR_SETENV, "SPECIAL_PATH", "/foo/bar");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "SetEnv SPECIAL_PATH /foo/bar\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, SetEnvIf) {
    auto *d = make_dir(DIR_SETENVIF, "no_gzip", "1");
    d->data.envif.attribute = strdup("User-Agent");
    d->data.envif.pattern = strdup("MSIE");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "SetEnvIf User-Agent MSIE no_gzip=1\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, BrowserMatch) {
    auto *d = make_dir(DIR_BROWSER_MATCH, "no_gzip", "1");
    d->data.envif.attribute = strdup("User-Agent");
    d->data.envif.pattern = strdup("Googlebot");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "BrowserMatch Googlebot no_gzip=1\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, SetEnvIfNoCase) {
    auto *d = make_dir(DIR_SETENVIF_NOCASE, "is_image", "1");
    d->data.envif.attribute = strdup("Request_URI");
    d->data.envif.pattern = strdup("\\.gif$");
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "SetEnvIfNoCase Request_URI \\.gif$ is_image=1\n");
    free(out);
    htaccess_directives_free(d);
}

/* ---- Brute force protection directives ---- */

TEST(PrinterTest, BruteForceProtectionOn) {
    auto *d = make_dir(DIR_BRUTE_FORCE_PROTECTION, nullptr, nullptr);
    d->data.brute_force.enabled = 1;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "BruteForceProtection On\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, BruteForceProtectionOff) {
    auto *d = make_dir(DIR_BRUTE_FORCE_PROTECTION, nullptr, nullptr);
    d->data.brute_force.enabled = 0;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "BruteForceProtection Off\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, BruteForceAllowedAttempts) {
    auto *d = make_dir(DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS, nullptr, nullptr);
    d->data.brute_force.allowed_attempts = 5;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "BruteForceAllowedAttempts 5\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, BruteForceWindow) {
    auto *d = make_dir(DIR_BRUTE_FORCE_WINDOW, nullptr, nullptr);
    d->data.brute_force.window_sec = 600;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "BruteForceWindow 600\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, BruteForceActionBlock) {
    auto *d = make_dir(DIR_BRUTE_FORCE_ACTION, nullptr, nullptr);
    d->data.brute_force.action = BF_ACTION_BLOCK;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "BruteForceAction block\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, BruteForceActionThrottle) {
    auto *d = make_dir(DIR_BRUTE_FORCE_ACTION, nullptr, nullptr);
    d->data.brute_force.action = BF_ACTION_THROTTLE;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "BruteForceAction throttle\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, BruteForceActionLog) {
    auto *d = make_dir(DIR_BRUTE_FORCE_ACTION, nullptr, nullptr);
    d->data.brute_force.action = BF_ACTION_LOG;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "BruteForceAction log\n");
    free(out);
    htaccess_directives_free(d);
}

TEST(PrinterTest, BruteForceThrottleDuration) {
    auto *d = make_dir(DIR_BRUTE_FORCE_THROTTLE_DURATION, nullptr, nullptr);
    d->data.brute_force.throttle_ms = 5000;
    char *out = htaccess_print(d);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "BruteForceThrottleDuration 5000\n");
    free(out);
    htaccess_directives_free(d);
}

/* ---- FilesMatch block ---- */

TEST(PrinterTest, FilesMatchWithChildren) {
    auto *fm = make_dir(DIR_FILES_MATCH, nullptr, nullptr);
    fm->data.files_match.pattern = strdup("\\.php$");

    auto *child1 = make_dir(DIR_HEADER_SET, "X-Content-Type-Options", "nosniff");
    auto *child2 = make_dir(DIR_HEADER_UNSET, "Server", nullptr);
    child1->next = child2;
    fm->data.files_match.children = child1;

    char *out = htaccess_print(fm);
    ASSERT_NE(out, nullptr);
    std::string expected =
        "<FilesMatch \"\\.php$\">\n"
        "Header set X-Content-Type-Options nosniff\n"
        "Header unset Server\n"
        "</FilesMatch>\n";
    EXPECT_EQ(std::string(out), expected);
    free(out);
    htaccess_directives_free(fm);
}

TEST(PrinterTest, FilesMatchEmpty) {
    auto *fm = make_dir(DIR_FILES_MATCH, nullptr, nullptr);
    fm->data.files_match.pattern = strdup(".*");
    fm->data.files_match.children = nullptr;

    char *out = htaccess_print(fm);
    ASSERT_NE(out, nullptr);
    std::string expected =
        "<FilesMatch \".*\">\n"
        "</FilesMatch>\n";
    EXPECT_EQ(std::string(out), expected);
    free(out);
    htaccess_directives_free(fm);
}

/* ---- Multiple directives ---- */

TEST(PrinterTest, MultipleDirectives) {
    auto *d1 = make_dir(DIR_HEADER_SET, "X-Frame-Options", "DENY");
    auto *d2 = make_dir(DIR_PHP_VALUE, "upload_max_filesize", "64M");
    d1->next = d2;

    char *out = htaccess_print(d1);
    ASSERT_NE(out, nullptr);
    std::string expected =
        "Header set X-Frame-Options DENY\n"
        "php_value upload_max_filesize 64M\n";
    EXPECT_EQ(std::string(out), expected);
    free(out);
    htaccess_directives_free(d1);
}

/* ---- Round-trip: print → parse → compare ---- */

TEST(PrinterTest, RoundTripSimple) {
    const char *input =
        "Header set X-Frame-Options DENY\n"
        "php_value upload_max_filesize 64M\n"
        "Order Allow,Deny\n"
        "Allow from 192.168.1.0/24\n"
        "Deny from all\n";

    /* Parse */
    htaccess_directive_t *parsed = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(parsed, nullptr);

    /* Print */
    char *printed = htaccess_print(parsed);
    ASSERT_NE(printed, nullptr);

    /* Re-parse */
    htaccess_directive_t *reparsed = htaccess_parse(printed, strlen(printed), "test2");
    ASSERT_NE(reparsed, nullptr);

    /* Compare: walk both lists and check types match */
    const htaccess_directive_t *a = parsed;
    const htaccess_directive_t *b = reparsed;
    while (a && b) {
        EXPECT_EQ(a->type, b->type);
        if (a->name && b->name)
            EXPECT_STREQ(a->name, b->name);
        if (a->value && b->value)
            EXPECT_STREQ(a->value, b->value);
        a = a->next;
        b = b->next;
    }
    EXPECT_EQ(a, nullptr);
    EXPECT_EQ(b, nullptr);

    free(printed);
    htaccess_directives_free(parsed);
    htaccess_directives_free(reparsed);
}

TEST(PrinterTest, RoundTripFilesMatch) {
    const char *input =
        "<FilesMatch \"\\.php$\">\n"
        "Header set X-Content-Type-Options nosniff\n"
        "</FilesMatch>\n";

    htaccess_directive_t *parsed = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->type, DIR_FILES_MATCH);

    char *printed = htaccess_print(parsed);
    ASSERT_NE(printed, nullptr);

    htaccess_directive_t *reparsed = htaccess_parse(printed, strlen(printed), "test2");
    ASSERT_NE(reparsed, nullptr);
    EXPECT_EQ(reparsed->type, DIR_FILES_MATCH);
    EXPECT_STREQ(reparsed->data.files_match.pattern,
                 parsed->data.files_match.pattern);

    /* Check nested child */
    ASSERT_NE(reparsed->data.files_match.children, nullptr);
    EXPECT_EQ(reparsed->data.files_match.children->type, DIR_HEADER_SET);
    EXPECT_STREQ(reparsed->data.files_match.children->name,
                 "X-Content-Type-Options");

    free(printed);
    htaccess_directives_free(parsed);
    htaccess_directives_free(reparsed);
}
