/**
 * test_exec_rewrite.cpp - Unit tests for RewriteRule execution layer
 *
 * Tests the module-side rewrite execution:
 * - RewriteEngine On/Off detection
 * - rebuild_rewrite_text() output correctness
 * - exec_rewrite_rules() with NULL g_api (stock OLS)
 *
 * Note: Actual regex matching and URI rewriting are handled by OLS's
 * RewriteEngine, not tested here. These tests verify the module's
 * parsing → text reconstruction → API delegation path.
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <string>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_parser.h"
#include "htaccess_directive.h"
#include "htaccess_exec_rewrite.h"
}

class ExecRewriteTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }

    MockSession session_;

    htaccess_directive_t *parse(const std::string &content) {
        return htaccess_parse(content.c_str(), content.size(), "/test/.htaccess");
    }
};

/* ================================================================== */
/*  RewriteEngine On/Off detection                                     */
/* ================================================================== */

TEST_F(ExecRewriteTest, EngineOffReturnsZero) {
    auto *d = parse(
        "RewriteEngine Off\n"
        "RewriteRule ^test$ /new [L]\n"
    );
    ASSERT_NE(d, nullptr);

    int rc = exec_rewrite_rules(session_.handle(), d);
    EXPECT_EQ(rc, 0); /* Engine off → no processing */

    htaccess_directives_free(d);
}

TEST_F(ExecRewriteTest, NoEngineDirectiveReturnsZero) {
    auto *d = parse("RewriteRule ^test$ /new [L]\n");
    ASSERT_NE(d, nullptr);

    int rc = exec_rewrite_rules(session_.handle(), d);
    EXPECT_EQ(rc, 0); /* No RewriteEngine directive → off by default */

    htaccess_directives_free(d);
}

TEST_F(ExecRewriteTest, EngineOnStockOlsReturnsNegative) {
    /* g_api is NULL in test mode → stock OLS, no rewrite support */
    auto *d = parse(
        "RewriteEngine On\n"
        "RewriteRule ^test$ /new [L]\n"
    );
    ASSERT_NE(d, nullptr);

    int rc = exec_rewrite_rules(session_.handle(), d);
    EXPECT_EQ(rc, -1); /* Stock OLS → unsupported */

    htaccess_directives_free(d);
}

TEST_F(ExecRewriteTest, NoRewriteRulesReturnsZero) {
    auto *d = parse("RewriteEngine On\n");
    ASSERT_NE(d, nullptr);

    /* Engine On but no rules → g_api is NULL → returns -1 (unsupported) */
    int rc = exec_rewrite_rules(session_.handle(), d);
    EXPECT_EQ(rc, -1);

    htaccess_directives_free(d);
}

/* ================================================================== */
/*  rebuild_rewrite_text() correctness                                 */
/* ================================================================== */

TEST_F(ExecRewriteTest, RebuildWordPress) {
    auto *d = parse(
        "RewriteEngine On\n"
        "RewriteBase /\n"
        "RewriteCond %{REQUEST_FILENAME} !-f\n"
        "RewriteCond %{REQUEST_FILENAME} !-d\n"
        "RewriteRule . /index.php [L]\n"
    );
    ASSERT_NE(d, nullptr);

    int text_len = 0;
    char *text = rebuild_rewrite_text(d, &text_len);
    ASSERT_NE(text, nullptr);
    EXPECT_GT(text_len, 0);

    /* Should contain RewriteBase, RewriteCond and RewriteRule lines */
    std::string t(text, text_len);
    EXPECT_NE(t.find("RewriteBase /"), std::string::npos);
    EXPECT_NE(t.find("RewriteCond %{REQUEST_FILENAME} !-f"), std::string::npos);
    EXPECT_NE(t.find("RewriteCond %{REQUEST_FILENAME} !-d"), std::string::npos);
    EXPECT_NE(t.find("RewriteRule . /index.php [L]"), std::string::npos);

    /* Should NOT contain RewriteEngine (handled separately by exec logic) */
    EXPECT_EQ(t.find("RewriteEngine"), std::string::npos);

    free(text);
    htaccess_directives_free(d);
}

TEST_F(ExecRewriteTest, RebuildHTTPSRedirect) {
    auto *d = parse(
        "RewriteEngine On\n"
        "RewriteCond %{HTTPS} off\n"
        "RewriteRule (.*) https://%{HTTP_HOST}/$1 [R=301,L]\n"
    );
    ASSERT_NE(d, nullptr);

    int text_len = 0;
    char *text = rebuild_rewrite_text(d, &text_len);
    ASSERT_NE(text, nullptr);

    std::string t(text, text_len);
    EXPECT_NE(t.find("RewriteCond %{HTTPS} off"), std::string::npos);
    EXPECT_NE(t.find("[R=301,L]"), std::string::npos);

    free(text);
    htaccess_directives_free(d);
}

TEST_F(ExecRewriteTest, RebuildCondFlags) {
    auto *d = parse(
        "RewriteEngine On\n"
        "RewriteCond %{HTTP_HOST} ^www\\. [NC,OR]\n"
        "RewriteCond %{HTTPS} off [NC]\n"
        "RewriteRule (.*) https://example.com/$1 [R=301,L]\n"
    );
    ASSERT_NE(d, nullptr);

    int text_len = 0;
    char *text = rebuild_rewrite_text(d, &text_len);
    ASSERT_NE(text, nullptr);

    std::string t(text, text_len);
    EXPECT_NE(t.find("[NC,OR]"), std::string::npos);
    EXPECT_NE(t.find("[NC]"), std::string::npos);

    free(text);
    htaccess_directives_free(d);
}

TEST_F(ExecRewriteTest, RebuildNoRulesReturnsNull) {
    auto *d = parse("RewriteEngine On\n");
    ASSERT_NE(d, nullptr);

    int text_len = 0;
    char *text = rebuild_rewrite_text(d, &text_len);
    EXPECT_EQ(text, nullptr);
    EXPECT_EQ(text_len, 0);

    htaccess_directives_free(d);
}

TEST_F(ExecRewriteTest, RebuildMultipleRules) {
    auto *d = parse(
        "RewriteEngine On\n"
        "RewriteRule ^old1$ /new1 [R=301,L]\n"
        "RewriteRule ^old2$ /new2 [R=302,L]\n"
        "RewriteRule .* - [F]\n"
    );
    ASSERT_NE(d, nullptr);

    int text_len = 0;
    char *text = rebuild_rewrite_text(d, &text_len);
    ASSERT_NE(text, nullptr);

    std::string t(text, text_len);
    EXPECT_NE(t.find("RewriteRule ^old1$ /new1 [R=301,L]"), std::string::npos);
    EXPECT_NE(t.find("RewriteRule ^old2$ /new2 [R=302,L]"), std::string::npos);
    EXPECT_NE(t.find("RewriteRule .* - [F]"), std::string::npos);

    free(text);
    htaccess_directives_free(d);
}

/* ------------------------------------------------------------------ */
/*  rebuild_rewrite_text: RewriteCond flags_raw lossless round-trip     */
/* ------------------------------------------------------------------ */

TEST_F(ExecRewriteTest, RebuildRewriteTextPreservesCondFlags)
{
    htaccess_directive_t *d = parse(
        "RewriteEngine On\n"
        "RewriteCond %{REQUEST_FILENAME} !-f [NC,OR]\n"
        "RewriteCond %{REQUEST_FILENAME} !-d\n"
        "RewriteRule ^(.*)$ index.php [L,QSA]\n");
    ASSERT_NE(d, nullptr);

    int text_len = 0;
    char *text = rebuild_rewrite_text(d, &text_len);
    ASSERT_NE(text, nullptr);

    std::string t(text, text_len);
    EXPECT_NE(t.find("[NC,OR]"), std::string::npos)
        << "RewriteCond [NC,OR] flags should be preserved";
    EXPECT_NE(t.find("RewriteCond %{REQUEST_FILENAME} !-d\n"),
              std::string::npos)
        << "Unflagged RewriteCond should appear without flags";

    free(text);
    htaccess_directives_free(d);
}

/* ------------------------------------------------------------------ */
/*  Fingerprint tests: different flags/conds produce distinct text      */
/* ------------------------------------------------------------------ */

TEST_F(ExecRewriteTest, DifferentRuleFlagsProduceDifferentText)
{
    htaccess_directive_t *d1 = parse(
        "RewriteEngine On\n"
        "RewriteRule ^(.*)$ /new [L]\n");
    htaccess_directive_t *d2 = parse(
        "RewriteEngine On\n"
        "RewriteRule ^(.*)$ /new [R=301,L]\n");
    ASSERT_NE(d1, nullptr);
    ASSERT_NE(d2, nullptr);

    int len1 = 0, len2 = 0;
    char *t1 = rebuild_rewrite_text(d1, &len1);
    char *t2 = rebuild_rewrite_text(d2, &len2);
    ASSERT_NE(t1, nullptr);
    ASSERT_NE(t2, nullptr);

    EXPECT_NE(std::string(t1, len1), std::string(t2, len2))
        << "Changing [L] to [R=301,L] must produce different text";

    free(t1);
    free(t2);
    htaccess_directives_free(d1);
    htaccess_directives_free(d2);
}

TEST_F(ExecRewriteTest, DifferentCondPatternsProduceDifferentText)
{
    htaccess_directive_t *d1 = parse(
        "RewriteEngine On\n"
        "RewriteCond %{REQUEST_FILENAME} !-f\n"
        "RewriteRule ^(.*)$ index.php [L]\n");
    htaccess_directive_t *d2 = parse(
        "RewriteEngine On\n"
        "RewriteCond %{REQUEST_FILENAME} !-d\n"
        "RewriteRule ^(.*)$ index.php [L]\n");
    ASSERT_NE(d1, nullptr);
    ASSERT_NE(d2, nullptr);

    int len1 = 0, len2 = 0;
    char *t1 = rebuild_rewrite_text(d1, &len1);
    char *t2 = rebuild_rewrite_text(d2, &len2);
    ASSERT_NE(t1, nullptr);
    ASSERT_NE(t2, nullptr);

    EXPECT_NE(std::string(t1, len1), std::string(t2, len2))
        << "Changing !-f to !-d must produce different text";

    free(t1);
    free(t2);
    htaccess_directives_free(d1);
    htaccess_directives_free(d2);
}
