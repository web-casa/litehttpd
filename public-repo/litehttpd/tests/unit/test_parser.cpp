/**
 * test_parser.cpp - Unit tests for htaccess_parse()
 *
 * Tests parsing of all directive types, comments, empty lines,
 * syntax errors, FilesMatch blocks, and order preservation.
 *
 * Validates: Requirements 2.1, 2.2, 2.3, 2.4, 9.1
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <string>

extern "C" {
#include "htaccess_parser.h"
#include "htaccess_printer.h"
#include "htaccess_directive.h"
}

#include "mock_lsiapi.h"

class ParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
    }

    htaccess_directive_t *parse(const std::string &content) {
        return htaccess_parse(content.c_str(), content.size(), "/test/.htaccess");
    }
};

/* ---- Empty / comment / null input ---- */

TEST_F(ParserTest, NullContentReturnsNull) {
    EXPECT_EQ(htaccess_parse(nullptr, 0, "/test"), nullptr);
}

TEST_F(ParserTest, EmptyContentReturnsNull) {
    EXPECT_EQ(htaccess_parse("", 0, "/test"), nullptr);
}

TEST_F(ParserTest, OnlyCommentsReturnsNull) {
    auto *d = parse("# This is a comment\n# Another comment\n");
    EXPECT_EQ(d, nullptr);
}

TEST_F(ParserTest, OnlyEmptyLinesReturnsNull) {
    auto *d = parse("\n\n\n");
    EXPECT_EQ(d, nullptr);
}

/* ---- Header directives ---- */

TEST_F(ParserTest, HeaderSet) {
    auto *d = parse("Header set X-Frame-Options DENY\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_HEADER_SET);
    EXPECT_STREQ(d->name, "X-Frame-Options");
    EXPECT_STREQ(d->value, "DENY");
    EXPECT_EQ(d->line_number, 1);
    EXPECT_EQ(d->next, nullptr);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, HeaderUnset) {
    auto *d = parse("Header unset Server\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_HEADER_UNSET);
    EXPECT_STREQ(d->name, "Server");
    EXPECT_EQ(d->value, nullptr);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, HeaderAppend) {
    auto *d = parse("Header append Cache-Control no-transform\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_HEADER_APPEND);
    EXPECT_STREQ(d->name, "Cache-Control");
    EXPECT_STREQ(d->value, "no-transform");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, HeaderMerge) {
    auto *d = parse("Header merge Cache-Control public\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_HEADER_MERGE);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, HeaderAdd) {
    auto *d = parse("Header add Set-Cookie \"session=abc\"\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_HEADER_ADD);
    EXPECT_STREQ(d->name, "Set-Cookie");
    EXPECT_STREQ(d->value, "session=abc");
    htaccess_directives_free(d);
}

/* ---- RequestHeader directives ---- */

TEST_F(ParserTest, RequestHeaderSet) {
    auto *d = parse("RequestHeader set X-Forwarded-Proto https\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REQUEST_HEADER_SET);
    EXPECT_STREQ(d->name, "X-Forwarded-Proto");
    EXPECT_STREQ(d->value, "https");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RequestHeaderUnset) {
    auto *d = parse("RequestHeader unset Proxy\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REQUEST_HEADER_UNSET);
    EXPECT_STREQ(d->name, "Proxy");
    htaccess_directives_free(d);
}

/* ---- PHP directives ---- */

TEST_F(ParserTest, PhpValue) {
    auto *d = parse("php_value upload_max_filesize 64M\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_PHP_VALUE);
    EXPECT_STREQ(d->name, "upload_max_filesize");
    EXPECT_STREQ(d->value, "64M");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, PhpFlag) {
    auto *d = parse("php_flag display_errors on\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_PHP_FLAG);
    EXPECT_STREQ(d->name, "display_errors");
    EXPECT_STREQ(d->value, "on");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, PhpFlagOff) {
    auto *d = parse("php_flag display_errors Off\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_PHP_FLAG);
    EXPECT_STREQ(d->value, "Off");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, PhpFlagInvalidValue) {
    auto *d = parse("php_flag display_errors maybe\n");
    EXPECT_EQ(d, nullptr);
    /* Should have logged a warning */
    EXPECT_FALSE(mock_lsiapi::get_log_records().empty());
}

TEST_F(ParserTest, PhpAdminValue) {
    auto *d = parse("php_admin_value open_basedir /var/www\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_PHP_ADMIN_VALUE);
    EXPECT_STREQ(d->name, "open_basedir");
    EXPECT_STREQ(d->value, "/var/www");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, PhpAdminFlag) {
    auto *d = parse("php_admin_flag engine off\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_PHP_ADMIN_FLAG);
    EXPECT_STREQ(d->name, "engine");
    EXPECT_STREQ(d->value, "off");
    htaccess_directives_free(d);
}

/* ---- Access control directives ---- */

TEST_F(ParserTest, OrderAllowDeny) {
    auto *d = parse("Order Allow,Deny\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_ORDER);
    EXPECT_EQ(d->data.acl.order, ORDER_ALLOW_DENY);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, OrderDenyAllow) {
    auto *d = parse("Order Deny,Allow\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_ORDER);
    EXPECT_EQ(d->data.acl.order, ORDER_DENY_ALLOW);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, AllowFromCIDR) {
    auto *d = parse("Allow from 192.168.1.0/24\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_ALLOW_FROM);
    EXPECT_STREQ(d->value, "192.168.1.0/24");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, DenyFromAll) {
    auto *d = parse("Deny from all\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_DENY_FROM);
    EXPECT_STREQ(d->value, "all");
    htaccess_directives_free(d);
}

/* ---- Redirect directives ---- */

TEST_F(ParserTest, RedirectDefault302) {
    auto *d = parse("Redirect /old /new\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REDIRECT);
    EXPECT_STREQ(d->name, "/old");
    EXPECT_STREQ(d->value, "/new");
    EXPECT_EQ(d->data.redirect.status_code, 302);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RedirectWithStatus) {
    auto *d = parse("Redirect 301 /old-page https://example.com/new-page\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REDIRECT);
    EXPECT_STREQ(d->name, "/old-page");
    EXPECT_STREQ(d->value, "https://example.com/new-page");
    EXPECT_EQ(d->data.redirect.status_code, 301);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RedirectMatch) {
    auto *d = parse("RedirectMatch 301 ^/blog/(.*)$ https://newblog.com/$1\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REDIRECT_MATCH);
    EXPECT_STREQ(d->data.redirect.pattern, "^/blog/(.*)$");
    EXPECT_STREQ(d->value, "https://newblog.com/$1");
    EXPECT_EQ(d->data.redirect.status_code, 301);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RedirectMatchDefault302) {
    auto *d = parse("RedirectMatch ^/old/(.*) /new/$1\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REDIRECT_MATCH);
    EXPECT_EQ(d->data.redirect.status_code, 302);
    htaccess_directives_free(d);
}

/* ---- Redirect keyword forms ---- */

TEST_F(ParserTest, RedirectPermanentKeyword) {
    auto *d = parse("Redirect permanent /old https://example.com/new\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REDIRECT);
    EXPECT_EQ(d->data.redirect.status_code, 301);
    EXPECT_STREQ(d->name, "/old");
    EXPECT_STREQ(d->value, "https://example.com/new");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RedirectTempKeyword) {
    auto *d = parse("Redirect temp /old https://example.com/new\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REDIRECT);
    EXPECT_EQ(d->data.redirect.status_code, 302);
    EXPECT_STREQ(d->name, "/old");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RedirectSeeotherKeyword) {
    auto *d = parse("Redirect seeother /old https://example.com/new\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REDIRECT);
    EXPECT_EQ(d->data.redirect.status_code, 303);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RedirectGoneKeyword) {
    auto *d = parse("Redirect gone /removed\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REDIRECT);
    EXPECT_EQ(d->data.redirect.status_code, 410);
    EXPECT_STREQ(d->name, "/removed");
    EXPECT_EQ(d->value, nullptr);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RedirectKeywordCaseInsensitive) {
    auto *d = parse("Redirect PERMANENT /old https://example.com/new\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->data.redirect.status_code, 301);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RedirectMatchPermanentKeyword) {
    auto *d = parse("RedirectMatch permanent ^/blog/(.*)$ https://newblog.com/$1\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REDIRECT_MATCH);
    EXPECT_EQ(d->data.redirect.status_code, 301);
    EXPECT_STREQ(d->data.redirect.pattern, "^/blog/(.*)$");
    EXPECT_STREQ(d->value, "https://newblog.com/$1");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RedirectMatchGoneKeyword) {
    auto *d = parse("RedirectMatch gone ^/removed/\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REDIRECT_MATCH);
    EXPECT_EQ(d->data.redirect.status_code, 410);
    EXPECT_STREQ(d->data.redirect.pattern, "^/removed/");
    EXPECT_EQ(d->value, nullptr);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, Redirect410NumericGone) {
    auto *d = parse("Redirect 410 /removed\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REDIRECT);
    EXPECT_EQ(d->data.redirect.status_code, 410);
    EXPECT_STREQ(d->name, "/removed");
    EXPECT_EQ(d->value, nullptr);
    htaccess_directives_free(d);
}

/* ---- ErrorDocument ---- */

TEST_F(ParserTest, ErrorDocumentPath) {
    auto *d = parse("ErrorDocument 404 /errors/404.html\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_ERROR_DOCUMENT);
    EXPECT_EQ(d->data.error_doc.error_code, 404);
    EXPECT_STREQ(d->value, "/errors/404.html");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, ErrorDocumentQuotedMessage) {
    auto *d = parse("ErrorDocument 503 \"Service Temporarily Unavailable\"\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_ERROR_DOCUMENT);
    EXPECT_EQ(d->data.error_doc.error_code, 503);
    /* After bug fix (task 1.2): leading quote is preserved so the executor
       can detect text message mode via value[0] == '"' */
    EXPECT_EQ(d->value[0], '"');
    EXPECT_TRUE(strstr(d->value, "Service Temporarily Unavailable") != nullptr);
    htaccess_directives_free(d);
}

/* ---- Expires directives ---- */

TEST_F(ParserTest, ExpiresActiveOn) {
    auto *d = parse("ExpiresActive On\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_EXPIRES_ACTIVE);
    EXPECT_EQ(d->data.expires.active, 1);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, ExpiresActiveOff) {
    auto *d = parse("ExpiresActive Off\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_EXPIRES_ACTIVE);
    EXPECT_EQ(d->data.expires.active, 0);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, ExpiresByType) {
    auto *d = parse("ExpiresByType image/jpeg \"access plus 1 month\"\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_EXPIRES_BY_TYPE);
    EXPECT_STREQ(d->name, "image/jpeg");
    EXPECT_EQ(d->data.expires.duration_sec, 2592000L);
    htaccess_directives_free(d);
}

/* ---- Environment variable directives ---- */

TEST_F(ParserTest, SetEnv) {
    auto *d = parse("SetEnv APP_ENV production\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_SETENV);
    EXPECT_STREQ(d->name, "APP_ENV");
    EXPECT_STREQ(d->value, "production");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, SetEnvIf) {
    auto *d = parse("SetEnvIf Remote_Addr ^192\\.168 local=1\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_SETENVIF);
    EXPECT_STREQ(d->name, "local");
    EXPECT_STREQ(d->value, "1");
    EXPECT_STREQ(d->data.envif.attribute, "Remote_Addr");
    EXPECT_STREQ(d->data.envif.pattern, "^192\\.168");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, SetEnvIfNoCase) {
    auto *d = parse("SetEnvIfNoCase Request_URI \\.gif$ is_image=1\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_SETENVIF_NOCASE);
    EXPECT_STREQ(d->name, "is_image");
    EXPECT_STREQ(d->value, "1");
    EXPECT_STREQ(d->data.envif.attribute, "Request_URI");
    EXPECT_STREQ(d->data.envif.pattern, "\\.gif$");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, BrowserMatch) {
    auto *d = parse("BrowserMatch Googlebot is_bot=1\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_BROWSER_MATCH);
    EXPECT_STREQ(d->name, "is_bot");
    EXPECT_STREQ(d->value, "1");
    EXPECT_STREQ(d->data.envif.attribute, "User-Agent");
    EXPECT_STREQ(d->data.envif.pattern, "Googlebot");
    htaccess_directives_free(d);
}

/* ---- Brute force directives ---- */

TEST_F(ParserTest, BruteForceProtectionOn) {
    auto *d = parse("BruteForceProtection On\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_BRUTE_FORCE_PROTECTION);
    EXPECT_EQ(d->data.brute_force.enabled, 1);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, BruteForceProtectionOff) {
    auto *d = parse("BruteForceProtection Off\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_BRUTE_FORCE_PROTECTION);
    EXPECT_EQ(d->data.brute_force.enabled, 0);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, BruteForceAllowedAttempts) {
    auto *d = parse("BruteForceAllowedAttempts 5\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS);
    EXPECT_EQ(d->data.brute_force.allowed_attempts, 5);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, BruteForceWindow) {
    auto *d = parse("BruteForceWindow 600\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_BRUTE_FORCE_WINDOW);
    EXPECT_EQ(d->data.brute_force.window_sec, 600);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, BruteForceActionBlock) {
    auto *d = parse("BruteForceAction block\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_BRUTE_FORCE_ACTION);
    EXPECT_EQ(d->data.brute_force.action, BF_ACTION_BLOCK);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, BruteForceActionThrottle) {
    auto *d = parse("BruteForceAction throttle\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_BRUTE_FORCE_ACTION);
    EXPECT_EQ(d->data.brute_force.action, BF_ACTION_THROTTLE);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, BruteForceActionLog) {
    auto *d = parse("BruteForceAction log\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_BRUTE_FORCE_ACTION);
    EXPECT_EQ(d->data.brute_force.action, BF_ACTION_LOG);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, BruteForceThrottleDuration) {
    auto *d = parse("BruteForceThrottleDuration 5000\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_BRUTE_FORCE_THROTTLE_DURATION);
    EXPECT_EQ(d->data.brute_force.throttle_ms, 5000);
    htaccess_directives_free(d);
}

/* ---- FilesMatch block ---- */

TEST_F(ParserTest, FilesMatchBlock) {
    std::string content =
        "<FilesMatch \"\\.php$\">\n"
        "Header set X-Content-Type-Options nosniff\n"
        "Header set X-Frame-Options SAMEORIGIN\n"
        "</FilesMatch>\n";

    auto *d = parse(content);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_FILES_MATCH);
    EXPECT_STREQ(d->data.files_match.pattern, "\\.php$");
    EXPECT_EQ(d->next, nullptr);

    /* Check children */
    auto *c1 = d->data.files_match.children;
    ASSERT_NE(c1, nullptr);
    EXPECT_EQ(c1->type, DIR_HEADER_SET);
    EXPECT_STREQ(c1->name, "X-Content-Type-Options");

    auto *c2 = c1->next;
    ASSERT_NE(c2, nullptr);
    EXPECT_EQ(c2->type, DIR_HEADER_SET);
    EXPECT_STREQ(c2->name, "X-Frame-Options");
    EXPECT_EQ(c2->next, nullptr);

    htaccess_directives_free(d);
}

TEST_F(ParserTest, UnclosedFilesMatchDiscarded) {
    std::string content =
        "<FilesMatch \"\\.php$\">\n"
        "Header set X-Test value\n";

    auto *d = parse(content);
    EXPECT_EQ(d, nullptr);
    /* Should have logged a warning about unclosed block */
    auto &logs = mock_lsiapi::get_log_records();
    EXPECT_FALSE(logs.empty());
    bool found_unclosed = false;
    for (auto &log : logs) {
        if (log.message.find("unclosed") != std::string::npos)
            found_unclosed = true;
    }
    EXPECT_TRUE(found_unclosed);
}

/* ---- Order preservation ---- */

TEST_F(ParserTest, PreservesDirectiveOrder) {
    std::string content =
        "Header set X-First one\n"
        "Header set X-Second two\n"
        "Header set X-Third three\n";

    auto *d = parse(content);
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d->name, "X-First");
    EXPECT_EQ(d->line_number, 1);

    ASSERT_NE(d->next, nullptr);
    EXPECT_STREQ(d->next->name, "X-Second");
    EXPECT_EQ(d->next->line_number, 2);

    ASSERT_NE(d->next->next, nullptr);
    EXPECT_STREQ(d->next->next->name, "X-Third");
    EXPECT_EQ(d->next->next->line_number, 3);

    EXPECT_EQ(d->next->next->next, nullptr);
    htaccess_directives_free(d);
}

/* ---- Syntax error handling ---- */

TEST_F(ParserTest, SyntaxErrorSkipsLine) {
    std::string content =
        "Header set X-Good value\n"
        "InvalidDirective something\n"
        "Header set X-Also-Good value2\n";

    auto *d = parse(content);
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d->name, "X-Good");

    ASSERT_NE(d->next, nullptr);
    EXPECT_STREQ(d->next->name, "X-Also-Good");
    EXPECT_EQ(d->next->next, nullptr);

    /* Should have logged a warning for the invalid line */
    auto &logs = mock_lsiapi::get_log_records();
    EXPECT_FALSE(logs.empty());
    htaccess_directives_free(d);
}

TEST_F(ParserTest, CommentsAndEmptyLinesSkipped) {
    std::string content =
        "# Comment line\n"
        "\n"
        "Header set X-Test value\n"
        "# Another comment\n"
        "\n";

    auto *d = parse(content);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_HEADER_SET);
    EXPECT_STREQ(d->name, "X-Test");
    EXPECT_EQ(d->next, nullptr);
    htaccess_directives_free(d);
}

/* ---- Multi-directive file ---- */

TEST_F(ParserTest, MultiDirectiveFile) {
    std::string content =
        "Order Deny,Allow\n"
        "Deny from all\n"
        "Allow from 10.0.0.0/8\n"
        "Header set X-Powered-By OLS\n"
        "php_value memory_limit 256M\n"
        "ExpiresActive On\n"
        "SetEnv APP_ENV staging\n"
        "BruteForceProtection On\n";

    auto *d = parse(content);
    ASSERT_NE(d, nullptr);

    int count = 0;
    for (auto *cur = d; cur; cur = cur->next)
        count++;
    EXPECT_EQ(count, 8);

    /* Verify types in order */
    EXPECT_EQ(d->type, DIR_ORDER);
    EXPECT_EQ(d->next->type, DIR_DENY_FROM);
    EXPECT_EQ(d->next->next->type, DIR_ALLOW_FROM);
    EXPECT_EQ(d->next->next->next->type, DIR_HEADER_SET);
    EXPECT_EQ(d->next->next->next->next->type, DIR_PHP_VALUE);
    EXPECT_EQ(d->next->next->next->next->next->type, DIR_EXPIRES_ACTIVE);
    EXPECT_EQ(d->next->next->next->next->next->next->type, DIR_SETENV);
    EXPECT_EQ(d->next->next->next->next->next->next->next->type, DIR_BRUTE_FORCE_PROTECTION);

    htaccess_directives_free(d);
}

/* ---- Line number tracking ---- */

TEST_F(ParserTest, LineNumbersCorrectWithCommentsAndBlanks) {
    std::string content =
        "# comment\n"
        "\n"
        "Header set X-A val\n"
        "# another comment\n"
        "Header set X-B val\n";

    auto *d = parse(content);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->line_number, 3);
    ASSERT_NE(d->next, nullptr);
    EXPECT_EQ(d->next->line_number, 5);
    htaccess_directives_free(d);
}

/* ---- FilesMatch with mixed directives ---- */

TEST_F(ParserTest, FilesMatchWithMixedDirectives) {
    std::string content =
        "Header set X-Global global\n"
        "<FilesMatch \"\\.js$\">\n"
        "Header set X-Content-Type application/javascript\n"
        "ExpiresActive On\n"
        "</FilesMatch>\n"
        "Header set X-After after\n";

    auto *d = parse(content);
    ASSERT_NE(d, nullptr);

    /* First: global header */
    EXPECT_EQ(d->type, DIR_HEADER_SET);
    EXPECT_STREQ(d->name, "X-Global");

    /* Second: FilesMatch block */
    auto *fm = d->next;
    ASSERT_NE(fm, nullptr);
    EXPECT_EQ(fm->type, DIR_FILES_MATCH);
    EXPECT_STREQ(fm->data.files_match.pattern, "\\.js$");

    auto *c1 = fm->data.files_match.children;
    ASSERT_NE(c1, nullptr);
    EXPECT_EQ(c1->type, DIR_HEADER_SET);
    auto *c2 = c1->next;
    ASSERT_NE(c2, nullptr);
    EXPECT_EQ(c2->type, DIR_EXPIRES_ACTIVE);

    /* Third: after header */
    auto *after_d = fm->next;
    ASSERT_NE(after_d, nullptr);
    EXPECT_EQ(after_d->type, DIR_HEADER_SET);
    EXPECT_STREQ(after_d->name, "X-After");

    htaccess_directives_free(d);
}

/* ================================================================== */
/*  Parser Negative Tests — verify invalid config is rejected          */
/* ================================================================== */

class ParserRejectTest : public ::testing::Test {
protected:
    void expect_null(const char *input) {
        auto *d = htaccess_parse(input, strlen(input), "<test>");
        EXPECT_EQ(d, nullptr) << "Expected NULL for: " << input;
        if (d) htaccess_directives_free(d);
    }
    void expect_empty(const char *input) {
        auto *d = htaccess_parse(input, strlen(input), "<test>");
        /* Parser may return NULL or a list with no recognized directives */
        if (d) htaccess_directives_free(d);
    }
};

/* --- Word boundary checks --- */

TEST_F(ParserRejectTest, OrderAllowDenyWithTrailingJunk) {
    expect_null("Order Allow,DenyXYZ");
}

TEST_F(ParserRejectTest, OrderDenyAllowWithTrailingJunk) {
    expect_null("Order Deny,AllowFoo");
}

TEST_F(ParserRejectTest, RequireAllGrantedWithTrailingJunk) {
    expect_null("Require all grantedfoo");
}

TEST_F(ParserRejectTest, RequireAllDeniedWithTrailingJunk) {
    expect_null("Require all deniedbar");
}

/* --- Missing required fields --- */

TEST_F(ParserRejectTest, HeaderSetMissingName) {
    expect_null("Header set");
}

TEST_F(ParserRejectTest, RedirectMissingTarget) {
    /* Redirect with path but no target (non-410) */
    expect_null("Redirect /old");
}

TEST_F(ParserRejectTest, PhpValueMissingName) {
    expect_null("php_value");
}

TEST_F(ParserRejectTest, SetEnvMissingValue) {
    /* SetEnv with name but no value — actually valid in Apache (sets empty) */
    /* Parser should accept this without crashing */
    auto *d = htaccess_parse("SetEnv MY_VAR", 13, "<test>");
    if (d) htaccess_directives_free(d);
}

TEST_F(ParserRejectTest, SetEnvIfMissingPattern) {
    expect_null("SetEnvIf Request_URI");
}

/* --- Boundary/edge cases --- */

TEST_F(ParserRejectTest, EmptyLine) {
    expect_empty("");
}

TEST_F(ParserRejectTest, PureWhitespace) {
    expect_empty("   \t  ");
}

TEST_F(ParserRejectTest, CommentOnly) {
    expect_empty("# This is a comment");
}

TEST_F(ParserRejectTest, UnknownDirective) {
    /* Unknown directives should be silently skipped */
    expect_empty("FooBarDirective some value");
}

TEST_F(ParserRejectTest, UnclosedIfModule) {
    /* IfModule without close — children should not leak into top level */
    auto *d = htaccess_parse("<IfModule mod_headers.c>\nHeader set X yes", 42, "<test>");
    /* Should not contain the Header as a top-level directive */
    /* (it's inside an unclosed IfModule, parser should warn and discard) */
    if (d) htaccess_directives_free(d);
}

TEST_F(ParserRejectTest, UnclosedFilesMatch) {
    auto *d = htaccess_parse("<FilesMatch \\.env$>\nOrder deny,allow", 36, "<test>");
    if (d) htaccess_directives_free(d);
}

TEST_F(ParserRejectTest, ClosingTagWithoutOpen) {
    /* </IfModule> without matching open — should be ignored */
    auto *d = htaccess_parse("</IfModule>\nHeader set X yes", 29, "<test>");
    if (d) {
        EXPECT_EQ(d->type, DIR_HEADER_SET);
        htaccess_directives_free(d);
    }
}

TEST_F(ParserRejectTest, ExpiresActiveInvalidValue) {
    /* ExpiresActive with non-on/off value */
    expect_null("ExpiresActive maybe");
}

TEST_F(ParserRejectTest, OrderInvalidValue) {
    expect_null("Order something,else");
}

/* ================================================================== */
/*  Round-trip Tests — parse → print → parse structural equivalence    */
/* ================================================================== */

class RoundTripQuotedTest : public ::testing::Test {
protected:
    void assert_roundtrip(const char *input) {
        /* Parse */
        auto *d1 = htaccess_parse(input, strlen(input), "<test>");
        ASSERT_NE(d1, nullptr) << "First parse failed for: " << input;

        /* Print */
        char *printed = htaccess_print(d1);
        ASSERT_NE(printed, nullptr) << "Print failed for: " << input;

        /* Re-parse */
        auto *d2 = htaccess_parse(printed, strlen(printed), "<test-rt>");
        ASSERT_NE(d2, nullptr) << "Re-parse failed for printed: " << printed;

        /* Compare: same count and types */
        int count1 = 0, count2 = 0;
        for (auto *d = d1; d; d = d->next) count1++;
        for (auto *d = d2; d; d = d->next) count2++;
        EXPECT_EQ(count1, count2) << "Directive count mismatch after round-trip";

        /* Compare type and name of each directive */
        auto *a = d1;
        auto *b = d2;
        while (a && b) {
            EXPECT_EQ(a->type, b->type);
            if (a->name && b->name)
                EXPECT_STREQ(a->name, b->name);
            if (a->value && b->value)
                EXPECT_STREQ(a->value, b->value);
            a = a->next;
            b = b->next;
        }

        htaccess_directives_free(d1);
        htaccess_directives_free(d2);
        free(printed);
    }
};

TEST_F(RoundTripQuotedTest, HeaderSetWithSpaces) {
    assert_roundtrip("Header set X-Test \"value with spaces\"");
}

TEST_F(RoundTripQuotedTest, RequestHeaderSetWithSpaces) {
    assert_roundtrip("RequestHeader set X-Proto \"https value\"");
}

TEST_F(RoundTripQuotedTest, SetEnvWithSpaces) {
    assert_roundtrip("SetEnv MY_VAR \"multi word value\"");
}

TEST_F(RoundTripQuotedTest, IfModuleWithQuotedName) {
    assert_roundtrip("<IfModule \"mod headers.c\">\nHeader set X yes\n</IfModule>");
}

TEST_F(RoundTripQuotedTest, FilesWithQuotedName) {
    assert_roundtrip("<Files \"my file.txt\">\nOrder deny,allow\nDeny from all\n</Files>");
}

TEST_F(RoundTripQuotedTest, SimpleHeaderNoQuotes) {
    assert_roundtrip("Header set X-Simple value");
}

TEST_F(RoundTripQuotedTest, RedirectSimple) {
    assert_roundtrip("Redirect 301 /old /new");
}

TEST_F(RoundTripQuotedTest, SetEnvIfWithPattern) {
    assert_roundtrip("SetEnvIf Request_URI ^/api MY_API=1");
}

TEST_F(RoundTripQuotedTest, PhpValue) {
    assert_roundtrip("php_value upload_max_filesize 128M");
}

TEST_F(RoundTripQuotedTest, ExpiresActive) {
    assert_roundtrip("ExpiresActive On");
}

/* ================================================================== */
/*  Additional negative + round-trip cases (行为一致性专项)             */
/* ================================================================== */

/* --- Parser: container tag with trailing junk --- */

TEST_F(ParserRejectTest, IfModuleTagTrailingJunk) {
    /* <IfModule mod_headers.c JUNK> should still parse (Apache ignores extra) */
    auto *d = htaccess_parse("<IfModule mod_headers.c extra>\nHeader set X y\n</IfModule>", 55, "<test>");
    if (d) htaccess_directives_free(d);
}

TEST_F(ParserRejectTest, FilesMatchEmptyPattern) {
    /* <FilesMatch ""> — empty regex pattern */
    auto *d = htaccess_parse("<FilesMatch \"\">\nHeader set X y\n</FilesMatch>", 43, "<test>");
    if (d) htaccess_directives_free(d);
}

/* --- Round-trip: php_value with path containing spaces --- */

TEST_F(RoundTripQuotedTest, PhpValueWithSpacePath) {
    assert_roundtrip("php_value auto_prepend_file \"/tmp/my file.php\"");
}

TEST_F(RoundTripQuotedTest, RedirectMatchQuotedPattern) {
    assert_roundtrip("RedirectMatch 301 \"^/old path$\" /new");
}

TEST_F(RoundTripQuotedTest, SetEnvIfQuotedPattern) {
    assert_roundtrip("SetEnvIf User-Agent \"Mozilla.*Firefox\" IS_FIREFOX=1");
}

TEST_F(RoundTripQuotedTest, HeaderEnvCondition) {
    /* Header with env= should not lose the condition on round-trip */
    auto *d1 = htaccess_parse("Header set X-Secure true env=HTTPS", 35, "<test>");
    ASSERT_NE(d1, nullptr);
    EXPECT_NE(d1->env_condition, nullptr);
    if (d1->env_condition) EXPECT_STREQ(d1->env_condition, "HTTPS");
    htaccess_directives_free(d1);
}

TEST_F(RoundTripQuotedTest, HeaderEditPattern) {
    /* Header edit should preserve edit_pattern through round-trip */
    auto *d1 = htaccess_parse("Header edit Cache-Control no-cache no-store", 44, "<test>");
    ASSERT_NE(d1, nullptr);
    EXPECT_EQ(d1->type, DIR_HEADER_EDIT);
    EXPECT_NE(d1->data.header_ext.edit_pattern, nullptr);
    htaccess_directives_free(d1);
}

/* ================================================================== */
/*  Rewrite directive parsing tests                                    */
/* ================================================================== */

TEST_F(ParserTest, RewriteEngineOn) {
    auto *d = parse("RewriteEngine On\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REWRITE_ENGINE);
    EXPECT_STREQ(d->name, "On");
    EXPECT_EQ(d->next, nullptr);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RewriteEngineOff) {
    auto *d = parse("RewriteEngine Off\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REWRITE_ENGINE);
    EXPECT_STREQ(d->name, "Off");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RewriteBase) {
    auto *d = parse("RewriteBase /blog/\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REWRITE_BASE);
    EXPECT_STREQ(d->value, "/blog/");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RewriteCondWithFlags) {
    /* Single RewriteCond without following RewriteRule → orphaned, discarded */
    auto *d = parse("RewriteCond %{HTTP_HOST} ^www\\. [NC,OR]\n");
    EXPECT_EQ(d, nullptr); /* orphaned cond discarded by htaccess_parse */
}

TEST_F(ParserTest, RewriteRuleBasic) {
    auto *d = parse("RewriteRule ^(.*)$ /index.php [L]\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REWRITE_RULE);
    EXPECT_STREQ(d->data.rewrite_rule.pattern, "^(.*)$");
    EXPECT_STREQ(d->value, "/index.php");
    ASSERT_NE(d->data.rewrite_rule.flags_raw, nullptr);
    EXPECT_STREQ(d->data.rewrite_rule.flags_raw, "[L]");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RewriteRuleNoFlags) {
    auto *d = parse("RewriteRule ^old$ /new\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_REWRITE_RULE);
    EXPECT_STREQ(d->data.rewrite_rule.pattern, "^old$");
    EXPECT_STREQ(d->value, "/new");
    EXPECT_EQ(d->data.rewrite_rule.flags_raw, nullptr);
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RewriteRuleMultiFlags) {
    auto *d = parse("RewriteRule ^(.*)$ /index.php [L,QSA,NC]\n");
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d->data.rewrite_rule.flags_raw, "[L,QSA,NC]");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RewriteRuleRedirect) {
    auto *d = parse("RewriteRule ^old$ https://example.com/new [R=301,L]\n");
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d->data.rewrite_rule.pattern, "^old$");
    EXPECT_STREQ(d->value, "https://example.com/new");
    EXPECT_STREQ(d->data.rewrite_rule.flags_raw, "[R=301,L]");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RewriteRuleDashSubstitution) {
    auto *d = parse("RewriteRule .* - [F]\n");
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d->value, "-");
    EXPECT_STREQ(d->data.rewrite_rule.flags_raw, "[F]");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, RewriteCondRuleLinking) {
    /* WordPress standard .htaccess */
    auto *d = parse(
        "RewriteEngine On\n"
        "RewriteBase /\n"
        "RewriteCond %{REQUEST_FILENAME} !-f\n"
        "RewriteCond %{REQUEST_FILENAME} !-d\n"
        "RewriteRule . /index.php [L]\n"
    );
    ASSERT_NE(d, nullptr);

    /* d[0] = RewriteEngine On */
    EXPECT_EQ(d->type, DIR_REWRITE_ENGINE);
    EXPECT_STREQ(d->name, "On");

    /* d[1] = RewriteBase / */
    ASSERT_NE(d->next, nullptr);
    auto *d2 = d->next;
    EXPECT_EQ(d2->type, DIR_REWRITE_BASE);
    EXPECT_STREQ(d2->value, "/");

    /* d[2] = RewriteRule with 2 conditions attached */
    ASSERT_NE(d2->next, nullptr);
    auto *rule = d2->next;
    EXPECT_EQ(rule->type, DIR_REWRITE_RULE);
    EXPECT_STREQ(rule->data.rewrite_rule.pattern, ".");
    EXPECT_STREQ(rule->value, "/index.php");

    /* Check conditions are linked */
    auto *cond1 = rule->data.rewrite_rule.conditions;
    ASSERT_NE(cond1, nullptr);
    EXPECT_EQ(cond1->type, DIR_REWRITE_COND);
    EXPECT_STREQ(cond1->name, "%{REQUEST_FILENAME}");
    EXPECT_STREQ(cond1->data.rewrite_cond.cond_pattern, "!-f");

    auto *cond2 = cond1->next;
    ASSERT_NE(cond2, nullptr);
    EXPECT_EQ(cond2->type, DIR_REWRITE_COND);
    EXPECT_STREQ(cond2->data.rewrite_cond.cond_pattern, "!-d");
    EXPECT_EQ(cond2->next, nullptr);

    /* No more directives */
    EXPECT_EQ(rule->next, nullptr);

    htaccess_directives_free(d);
}

TEST_F(ParserTest, RewriteMultipleRulesWithConds) {
    auto *d = parse(
        "RewriteEngine On\n"
        "RewriteCond %{HTTPS} off\n"
        "RewriteRule (.*) https://%{HTTP_HOST}/$1 [R=301,L]\n"
        "RewriteCond %{REQUEST_FILENAME} !-f\n"
        "RewriteRule ^(.*)$ /index.php [L,QSA]\n"
    );
    ASSERT_NE(d, nullptr);

    /* d[0] = RewriteEngine */
    EXPECT_EQ(d->type, DIR_REWRITE_ENGINE);

    /* d[1] = first RewriteRule (HTTPS redirect) with 1 condition */
    auto *r1 = d->next;
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(r1->type, DIR_REWRITE_RULE);
    ASSERT_NE(r1->data.rewrite_rule.conditions, nullptr);
    EXPECT_STREQ(r1->data.rewrite_rule.conditions->name, "%{HTTPS}");
    EXPECT_EQ(r1->data.rewrite_rule.conditions->next, nullptr);

    /* d[2] = second RewriteRule (index.php fallback) with 1 condition */
    auto *r2 = r1->next;
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(r2->type, DIR_REWRITE_RULE);
    ASSERT_NE(r2->data.rewrite_rule.conditions, nullptr);
    EXPECT_STREQ(r2->data.rewrite_rule.conditions->name, "%{REQUEST_FILENAME}");

    htaccess_directives_free(d);
}

TEST_F(ParserTest, RewriteRoundTrip) {
    const char *input =
        "RewriteEngine On\n"
        "RewriteBase /\n"
        "RewriteCond %{REQUEST_FILENAME} !-f\n"
        "RewriteCond %{REQUEST_FILENAME} !-d\n"
        "RewriteRule . /index.php [L]\n";

    auto *d = parse(input);
    ASSERT_NE(d, nullptr);

    /* Print */
    char *printed = htaccess_print(d);
    ASSERT_NE(printed, nullptr);

    /* Reparse */
    auto *d2 = htaccess_parse(printed, strlen(printed), "<roundtrip>");
    ASSERT_NE(d2, nullptr);

    /* Verify structure */
    EXPECT_EQ(d2->type, DIR_REWRITE_ENGINE);
    ASSERT_NE(d2->next, nullptr);
    EXPECT_EQ(d2->next->type, DIR_REWRITE_BASE);
    ASSERT_NE(d2->next->next, nullptr);
    auto *rule = d2->next->next;
    EXPECT_EQ(rule->type, DIR_REWRITE_RULE);
    ASSERT_NE(rule->data.rewrite_rule.conditions, nullptr);
    ASSERT_NE(rule->data.rewrite_rule.conditions->next, nullptr);

    htaccess_directives_free(d);
    htaccess_directives_free(d2);
    free(printed);
}

TEST_F(ParserTest, RewriteInvalidMissingPattern) {
    /* RewriteRule with no arguments */
    auto *d = parse("RewriteRule\n");
    EXPECT_EQ(d, nullptr);
}

TEST_F(ParserTest, RewriteInvalidMissingSubstitution) {
    /* RewriteRule with pattern but no substitution */
    auto *d = parse("RewriteRule ^test$\n");
    EXPECT_EQ(d, nullptr);
}

TEST_F(ParserTest, RewriteCondInvalid) {
    /* RewriteCond with no pattern */
    auto *d = parse("RewriteCond %{HOST}\n");
    EXPECT_EQ(d, nullptr);
}

/* ================================================================== */
/*  Phase 1 directive parsing tests                                    */
/* ================================================================== */

TEST_F(ParserTest, AddDefaultCharsetUTF8) {
    auto *d = parse("AddDefaultCharset UTF-8\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_ADD_DEFAULT_CHARSET);
    EXPECT_STREQ(d->value, "UTF-8");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, AddDefaultCharsetOff) {
    auto *d = parse("AddDefaultCharset Off\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_ADD_DEFAULT_CHARSET);
    EXPECT_STREQ(d->value, "Off");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, DefaultType) {
    auto *d = parse("DefaultType text/html\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_DEFAULT_TYPE);
    EXPECT_STREQ(d->value, "text/html");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, SatisfyAny) {
    auto *d = parse("Satisfy Any\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_SATISFY);
    EXPECT_STREQ(d->value, "Any");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, SatisfyAll) {
    auto *d = parse("Satisfy All\n");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_SATISFY);
    EXPECT_STREQ(d->value, "All");
    htaccess_directives_free(d);
}

TEST_F(ParserTest, Phase1RoundTrip) {
    const char *input =
        "AddDefaultCharset UTF-8\n"
        "DefaultType application/octet-stream\n"
        "Satisfy Any\n";
    auto *d = parse(input);
    ASSERT_NE(d, nullptr);

    char *printed = htaccess_print(d);
    ASSERT_NE(printed, nullptr);

    auto *d2 = htaccess_parse(printed, strlen(printed), "<roundtrip>");
    ASSERT_NE(d2, nullptr);

    EXPECT_EQ(d2->type, DIR_ADD_DEFAULT_CHARSET);
    EXPECT_STREQ(d2->value, "UTF-8");

    ASSERT_NE(d2->next, nullptr);
    EXPECT_EQ(d2->next->type, DIR_DEFAULT_TYPE);
    EXPECT_STREQ(d2->next->value, "application/octet-stream");

    ASSERT_NE(d2->next->next, nullptr);
    EXPECT_EQ(d2->next->next->type, DIR_SATISFY);
    EXPECT_STREQ(d2->next->next->value, "Any");

    htaccess_directives_free(d);
    htaccess_directives_free(d2);
    free(printed);
}
