/**
 * test_exec_auth.cpp - Unit tests for AuthType Basic executor
 *
 * Tests auth flow: no credentials → 401, wrong credentials → 401,
 * correct credentials → pass, missing AuthUserFile → 500.
 *
 * Validates: Requirements 10.1-10.8
 */
#include <gtest/gtest.h>
#include <cstring>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <string>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_auth.h"
#include "htaccess_directive.h"
#include "htaccess_parser.h"
#include "htaccess_printer.h"
}

static htaccess_directive_t *parse(const char *input) {
    return htaccess_parse(input, strlen(input), "test");
}

/* Base64 encode helper for "user:pass" */
static std::string base64_encode(const std::string &input) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

class AuthBasicTest : public ::testing::Test {
public:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
        /* Create a temp document root and place the htpasswd inside it.
         * exec_auth_basic() confines AuthUserFile to the document-root
         * subtree, so the test mirrors a realistic, allowed layout. */
        char dirbuf[] = "/tmp/authroot_XXXXXX";
        char *dir = mkdtemp(dirbuf);
        if (dir) {
            docroot_ = dir;
            session_.set_doc_root(docroot_);
            tmpfile_ = docroot_ + "/.htpasswd";
            FILE *f = fopen(tmpfile_.c_str(), "w");
            if (f) {
                /* testuser:testpass — generate crypt hash */
                const char *hash = crypt("testpass", "ab");
                fprintf(f, "testuser:%s\n", hash);
                fclose(f);
            }
        }
    }
    void TearDown() override {
        if (!tmpfile_.empty()) remove(tmpfile_.c_str());
        if (!docroot_.empty()) rmdir(docroot_.c_str());
    }
protected:
    MockSession session_;
    std::string tmpfile_;
    std::string docroot_;
};

/* No credentials → 401 */
TEST_F(AuthBasicTest, NoCredentials401)
{
    std::string input =
        "AuthType Basic\n"
        "AuthName \"Restricted\"\n"
        "AuthUserFile " + tmpfile_ + "\n"
        "Require valid-user\n";
    auto *dirs = parse(input.c_str());
    ASSERT_NE(dirs, nullptr);

    /* No auth header set */
    int rc = exec_auth_basic(session_.handle(), dirs);
    EXPECT_EQ(rc, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 401);
    EXPECT_FALSE(session_.get_www_authenticate().empty());

    htaccess_directives_free(dirs);
}

/* Wrong credentials → 401 */
TEST_F(AuthBasicTest, WrongCredentials401)
{
    std::string input =
        "AuthType Basic\n"
        "AuthName \"Restricted\"\n"
        "AuthUserFile " + tmpfile_ + "\n"
        "Require valid-user\n";
    auto *dirs = parse(input.c_str());
    ASSERT_NE(dirs, nullptr);

    std::string auth = "Basic " + base64_encode("testuser:wrongpass");
    session_.set_auth_header(auth);

    int rc = exec_auth_basic(session_.handle(), dirs);
    EXPECT_EQ(rc, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 401);

    htaccess_directives_free(dirs);
}

/* Correct credentials → pass */
TEST_F(AuthBasicTest, CorrectCredentialsPass)
{
    std::string input =
        "AuthType Basic\n"
        "AuthName \"Restricted\"\n"
        "AuthUserFile " + tmpfile_ + "\n"
        "Require valid-user\n";
    auto *dirs = parse(input.c_str());
    ASSERT_NE(dirs, nullptr);

    std::string auth = "Basic " + base64_encode("testuser:testpass");
    session_.set_auth_header(auth);

    int rc = exec_auth_basic(session_.handle(), dirs);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

/* Require user with matching username + correct password → pass */
TEST_F(AuthBasicTest, RequireUserMatchPasses)
{
    std::string input =
        "AuthType Basic\n"
        "AuthName \"Restricted\"\n"
        "AuthUserFile " + tmpfile_ + "\n"
        "Require user testuser alice\n";
    auto *dirs = parse(input.c_str());
    ASSERT_NE(dirs, nullptr);

    std::string auth = "Basic " + base64_encode("testuser:testpass");
    session_.set_auth_header(auth);

    int rc = exec_auth_basic(session_.handle(), dirs);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

/* Require user where the authenticated user is NOT listed → 403
 * (valid credentials, but not an allowed user). */
TEST_F(AuthBasicTest, RequireUserMismatch403)
{
    std::string input =
        "AuthType Basic\n"
        "AuthName \"Restricted\"\n"
        "AuthUserFile " + tmpfile_ + "\n"
        "Require user alice bob\n";
    auto *dirs = parse(input.c_str());
    ASSERT_NE(dirs, nullptr);

    std::string auth = "Basic " + base64_encode("testuser:testpass");
    session_.set_auth_header(auth);

    int rc = exec_auth_basic(session_.handle(), dirs);
    EXPECT_EQ(rc, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    htaccess_directives_free(dirs);
}

/* Require user without credentials → 401 (must NOT fail open). */
TEST_F(AuthBasicTest, RequireUserNoCredentials401)
{
    std::string input =
        "AuthType Basic\n"
        "AuthName \"Restricted\"\n"
        "AuthUserFile " + tmpfile_ + "\n"
        "Require user testuser\n";
    auto *dirs = parse(input.c_str());
    ASSERT_NE(dirs, nullptr);

    int rc = exec_auth_basic(session_.handle(), dirs);
    EXPECT_EQ(rc, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 401);

    htaccess_directives_free(dirs);
}

/* Unsupported "Require group" must parse to a fail-closed directive,
 * never be silently dropped (which would leave the dir unprotected). */
TEST_F(AuthBasicTest, RequireGroupParsesToFailClosed)
{
    auto *dirs = parse("Require group admins\n");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_REQUIRE_GROUP);

    /* And it must round-trip cleanly (not duplicate the "group" keyword). */
    char *printed = htaccess_print(dirs);
    ASSERT_NE(printed, nullptr);
    EXPECT_NE(strstr(printed, "Require group admins"), nullptr);
    EXPECT_EQ(strstr(printed, "group group"), nullptr);
    free(printed);

    htaccess_directives_free(dirs);
}

/* Require user inside <RequireAll> must be preserved (not dropped by the
 * container allowlist) AND enforced — a valid-user inside RequireAll must
 * NOT widen acceptance to any authenticated user. testuser is valid but
 * not "alice", so access must be denied (403). */
TEST_F(AuthBasicTest, RequireUserInRequireAllEnforced)
{
    std::string input =
        "AuthType Basic\n"
        "AuthName \"Restricted\"\n"
        "AuthUserFile " + tmpfile_ + "\n"
        "<RequireAll>\n"
        "Require valid-user\n"
        "Require user alice\n"
        "</RequireAll>\n";
    auto *dirs = parse(input.c_str());
    ASSERT_NE(dirs, nullptr);

    std::string auth = "Basic " + base64_encode("testuser:testpass");
    session_.set_auth_header(auth);

    int rc = exec_auth_basic(session_.handle(), dirs);
    EXPECT_EQ(rc, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    htaccess_directives_free(dirs);
}

/* Two distinct "Require user" lists inside <RequireAll> mean the user must
 * be in BOTH (AND). testuser is in the first list but not the second (alice),
 * so access must be denied. Regression: a flat-OR of the lists would have let
 * testuser through (fail-open). */
TEST_F(AuthBasicTest, RequireAllMultipleUserListsAreAnded)
{
    std::string input =
        "AuthType Basic\n"
        "AuthName \"Restricted\"\n"
        "AuthUserFile " + tmpfile_ + "\n"
        "<RequireAll>\n"
        "Require user testuser\n"
        "Require user alice\n"
        "</RequireAll>\n";
    auto *dirs = parse(input.c_str());
    ASSERT_NE(dirs, nullptr);

    std::string auth = "Basic " + base64_encode("testuser:testpass");
    session_.set_auth_header(auth);

    int rc = exec_auth_basic(session_.handle(), dirs);
    EXPECT_EQ(rc, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    htaccess_directives_free(dirs);
}

/* Same container, but the listed user (testuser) authenticates → pass. */
TEST_F(AuthBasicTest, RequireUserInRequireAllMatchPasses)
{
    std::string input =
        "AuthType Basic\n"
        "AuthName \"Restricted\"\n"
        "AuthUserFile " + tmpfile_ + "\n"
        "<RequireAll>\n"
        "Require valid-user\n"
        "Require user testuser\n"
        "</RequireAll>\n";
    auto *dirs = parse(input.c_str());
    ASSERT_NE(dirs, nullptr);

    std::string auth = "Basic " + base64_encode("testuser:testpass");
    session_.set_auth_header(auth);

    int rc = exec_auth_basic(session_.handle(), dirs);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

/* Missing AuthUserFile → 500 */
TEST_F(AuthBasicTest, MissingAuthUserFile500)
{
    const char *input =
        "AuthType Basic\n"
        "AuthName \"Restricted\"\n"
        "AuthUserFile /nonexistent/path/htpasswd\n"
        "Require valid-user\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    std::string auth = "Basic " + base64_encode("testuser:testpass");
    session_.set_auth_header(auth);

    int rc = exec_auth_basic(session_.handle(), dirs);
    EXPECT_EQ(rc, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 500);

    htaccess_directives_free(dirs);
}

/* No AuthType → no auth required */
TEST_F(AuthBasicTest, NoAuthTypeNoAuthRequired)
{
    const char *input = "Header set X-Test value\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_auth_basic(session_.handle(), dirs);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

/* htpasswd_check with crypt hash */
TEST_F(AuthBasicTest, HtpasswdCheckCrypt)
{
    const char *raw = crypt("hello", "ab");
    ASSERT_NE(raw, nullptr);
    /* Must copy — crypt() uses a static buffer */
    std::string hash(raw);
    EXPECT_EQ(htpasswd_check(hash.c_str(), "hello"), 1);
    EXPECT_EQ(htpasswd_check(hash.c_str(), "wrong"), 0);
}

/* Parsing round-trip for auth directives */
TEST_F(AuthBasicTest, ParseRoundTrip)
{
    const char *input =
        "AuthType Basic\n"
        "AuthName \"My Realm\"\n"
        "AuthUserFile /etc/htpasswd\n"
        "Require valid-user\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    /* Verify types */
    EXPECT_EQ(dirs->type, DIR_AUTH_TYPE);
    EXPECT_STREQ(dirs->value, "Basic");
    ASSERT_NE(dirs->next, nullptr);
    EXPECT_EQ(dirs->next->type, DIR_AUTH_NAME);
    EXPECT_STREQ(dirs->next->value, "My Realm");
    ASSERT_NE(dirs->next->next, nullptr);
    EXPECT_EQ(dirs->next->next->type, DIR_AUTH_USER_FILE);
    EXPECT_STREQ(dirs->next->next->value, "/etc/htpasswd");
    ASSERT_NE(dirs->next->next->next, nullptr);
    EXPECT_EQ(dirs->next->next->next->type, DIR_REQUIRE_VALID_USER);

    htaccess_directives_free(dirs);
}
