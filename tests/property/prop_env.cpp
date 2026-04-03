/**
 * prop_env.cpp - Property-based tests for SetEnvIf conditional setting
 *
 * Feature: ols-htaccess-module
 *
 * Property 22: SetEnvIf 条件设置
 *
 * Verifies that environment variables are set if and only if the
 * attribute value matches the regex pattern. Covers SetEnvIf and
 * BrowserMatch directives.
 *
 * **Validates: Requirements 11.2, 11.6**
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <cstring>
#include <cstdlib>

#include "mock_lsiapi.h"
#include "gen_header.h"
#include "gen_regex.h"

extern "C" {
#include "htaccess_exec_env.h"
#include "htaccess_directive.h"
int is_env_blacklisted(const char *name);
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_setenvif(const std::string &var_name,
                                           const std::string &var_value,
                                           const std::string &attribute,
                                           const std::string &pattern)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_SETENVIF;
    d->line_number = 1;
    d->name = strdup(var_name.c_str());
    d->value = strdup(var_value.c_str());
    d->data.envif.attribute = strdup(attribute.c_str());
    d->data.envif.pattern = strdup(pattern.c_str());
    d->next = nullptr;
    return d;
}

static htaccess_directive_t *make_browser_match(const std::string &var_name,
                                                const std::string &var_value,
                                                const std::string &pattern)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_BROWSER_MATCH;
    d->line_number = 1;
    d->name = strdup(var_name.c_str());
    d->value = strdup(var_value.c_str());
    d->data.envif.attribute = nullptr;
    d->data.envif.pattern = strdup(pattern.c_str());
    d->next = nullptr;
    return d;
}

static void free_dir(htaccess_directive_t *d)
{
    if (!d) return;
    free(d->name);
    free(d->value);
    free(d->data.envif.attribute);
    free(d->data.envif.pattern);
    free(d);
}

/* ------------------------------------------------------------------ */
/*  Generators                                                         */
/* ------------------------------------------------------------------ */

namespace gen {

/**
 * Generate a simple environment variable name (alphanumeric + underscore).
 */
inline rc::Gen<std::string> envVarName()
{
    static const std::string kEnvChars =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
    return rc::gen::mapcat(
        rc::gen::inRange(1, 16),
        [](int len) {
            return rc::gen::map(
                rc::gen::pair(
                    rc::gen::elementOf(std::string(
                        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")),
                    rc::gen::container<std::vector<char>>(
                        (std::size_t)(len - 1),
                        rc::gen::elementOf(kEnvChars))),
                [](const std::pair<char, std::vector<char>> &p) {
                    return std::string(1, p.first) +
                           std::string(p.second.begin(), p.second.end());
                });
        });
}

/**
 * Generate a simple env var value.
 */
inline rc::Gen<std::string> envVarValue()
{
    return simpleValue();
}

/**
 * Generate a SetEnvIf attribute name from the supported set.
 */
inline rc::Gen<std::string> envifAttribute()
{
    auto attrs = std::vector<std::string>{
        "Remote_Addr", "Request_URI", "User-Agent"};
    return rc::gen::elementOf(attrs);
}

/**
 * Generate a value that matches a given literal pattern.
 * The value contains the literal as a substring.
 */
inline rc::Gen<std::string> valueContaining(const std::string &literal)
{
    return rc::gen::map(
        rc::gen::pair(alphaIdent(), alphaIdent()),
        [literal](const std::pair<std::string, std::string> &p) {
            return p.first + literal + p.second;
        });
}

/**
 * Generate a value guaranteed NOT to contain a given literal.
 * Uses a completely different character set.
 */
inline rc::Gen<std::string> valueNotContaining(const std::string &literal)
{
    /* Generate a numeric-only string — the literal is alpha, so no overlap */
    return rc::gen::mapcat(
        rc::gen::inRange(3, 10),
        [](int len) {
            return rc::gen::map(
                rc::gen::container<std::vector<char>>(
                    (std::size_t)len,
                    rc::gen::elementOf(std::string("0123456789"))),
                [](const std::vector<char> &v) {
                    return std::string(v.begin(), v.end());
                });
        });
}

} /* namespace gen */

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class EnvPropertyFixture : public ::testing::Test {
public:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }

protected:
    MockSession session_;
};

/* ------------------------------------------------------------------ */
/*  Property 22: SetEnvIf 条件设置                                     */
/*                                                                     */
/*  For any request attribute value, regex pattern, and env variable,   */
/*  SetEnvIf/BrowserMatch sets the env var if and only if the          */
/*  attribute value matches the regex pattern.                          */
/*                                                                     */
/*  **Validates: Requirements 11.2, 11.6**                             */
/* ------------------------------------------------------------------ */

/**
 * When the attribute value matches the regex, the env var is set.
 */
RC_GTEST_FIXTURE_PROP(EnvPropertyFixture,
                      SetEnvIfSetsVarWhenAttributeMatches,
                      ())
{
    /* Generate a literal pattern and a value that contains it */
    auto literal = *gen::regexLiteral();
    auto attribute = *gen::envifAttribute();
    auto varName = *gen::envVarName();
    RC_PRE(!is_env_blacklisted(varName.c_str()));
    auto varValue = *gen::envVarValue();
    auto attrValue = *gen::valueContaining(literal);

    /* Set up the session attribute */
    if (attribute == "Remote_Addr") {
        session_.set_client_ip(attrValue);
    } else if (attribute == "Request_URI") {
        session_.set_request_uri(attrValue);
    } else {
        session_.add_request_header(attribute, attrValue);
    }

    /* Pattern: the literal string (matches as substring) */
    auto *dir = make_setenvif(varName, varValue, attribute, literal);

    int rc = exec_setenvif(session_.handle(), dir);
    RC_ASSERT(rc == LSI_OK);

    /* Env var should be set because attribute contains the literal */
    RC_ASSERT(session_.has_env_var(varName));
    RC_ASSERT(session_.get_env_var(varName) == varValue);

    free_dir(dir);
}

/**
 * When the attribute value does NOT match the regex, the env var is NOT set.
 */
RC_GTEST_FIXTURE_PROP(EnvPropertyFixture,
                      SetEnvIfDoesNotSetVarWhenNoMatch,
                      ())
{
    /* Generate a literal pattern and a value that does NOT contain it */
    auto literal = *gen::regexLiteral();
    auto attribute = *gen::envifAttribute();
    auto varName = *gen::envVarName();
    auto varValue = *gen::envVarValue();
    auto attrValue = *gen::valueNotContaining(literal);

    /* Set up the session attribute */
    if (attribute == "Remote_Addr") {
        session_.set_client_ip(attrValue);
    } else if (attribute == "Request_URI") {
        session_.set_request_uri(attrValue);
    } else {
        session_.add_request_header(attribute, attrValue);
    }

    /* Use ^literal$ to require exact match — numeric value won't match alpha literal */
    std::string pattern = "^" + literal + "$";
    auto *dir = make_setenvif(varName, varValue, attribute, pattern);

    int rc = exec_setenvif(session_.handle(), dir);
    RC_ASSERT(rc == LSI_OK);

    /* Env var should NOT be set */
    RC_ASSERT(!session_.has_env_var(varName));

    free_dir(dir);
}

/**
 * BrowserMatch sets env var when User-Agent matches the regex.
 */
RC_GTEST_FIXTURE_PROP(EnvPropertyFixture,
                      BrowserMatchSetsVarWhenUserAgentMatches,
                      ())
{
    auto literal = *gen::regexLiteral();
    auto varName = *gen::envVarName();
    RC_PRE(!is_env_blacklisted(varName.c_str()));
    auto varValue = *gen::envVarValue();
    auto uaValue = *gen::valueContaining(literal);

    session_.add_request_header("User-Agent", uaValue);

    auto *dir = make_browser_match(varName, varValue, literal);

    int rc = exec_browser_match(session_.handle(), dir);
    RC_ASSERT(rc == LSI_OK);

    RC_ASSERT(session_.has_env_var(varName));
    RC_ASSERT(session_.get_env_var(varName) == varValue);

    free_dir(dir);
}

/**
 * BrowserMatch does NOT set env var when User-Agent does not match.
 */
RC_GTEST_FIXTURE_PROP(EnvPropertyFixture,
                      BrowserMatchDoesNotSetVarWhenNoMatch,
                      ())
{
    auto literal = *gen::regexLiteral();
    auto varName = *gen::envVarName();
    auto varValue = *gen::envVarValue();
    auto uaValue = *gen::valueNotContaining(literal);

    session_.add_request_header("User-Agent", uaValue);

    std::string pattern = "^" + literal + "$";
    auto *dir = make_browser_match(varName, varValue, pattern);

    int rc = exec_browser_match(session_.handle(), dir);
    RC_ASSERT(rc == LSI_OK);

    RC_ASSERT(!session_.has_env_var(varName));

    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  SetEnvIfNoCase — case-insensitive pattern matching                 */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_setenvif_nocase(const std::string &var_name,
                                                  const std::string &var_value,
                                                  const std::string &attribute,
                                                  const std::string &pattern)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_SETENVIF_NOCASE;
    d->line_number = 1;
    d->name = strdup(var_name.c_str());
    d->value = strdup(var_value.c_str());
    d->data.envif.attribute = strdup(attribute.c_str());
    d->data.envif.pattern = strdup(pattern.c_str());
    d->next = nullptr;
    return d;
}

TEST_F(EnvPropertyFixture, SetEnvIfNoCaseMatchesCaseInsensitive)
{
    session_.set_request_uri("/path/IMAGE.GIF");

    auto *dir = make_setenvif_nocase("is_image", "1",
                                      "Request_URI", "\\.gif$");
    int rc = exec_setenvif(session_.handle(), dir);
    EXPECT_EQ(rc, LSI_OK);
    /* Should match because NoCase makes regex case-insensitive */
    EXPECT_TRUE(session_.has_env_var("is_image"));
    EXPECT_EQ(session_.get_env_var("is_image"), "1");
    free_dir(dir);
}

TEST_F(EnvPropertyFixture, SetEnvIfCaseSensitiveDoesNotMatch)
{
    session_.set_request_uri("/path/IMAGE.GIF");

    /* Regular SetEnvIf should NOT match .gif$ against IMAGE.GIF */
    auto *dir = make_setenvif("is_image", "1",
                               "Request_URI", "\\.gif$");
    int rc = exec_setenvif(session_.handle(), dir);
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_FALSE(session_.has_env_var("is_image"));
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  SetEnvIf Query_String attribute                                    */
/* ------------------------------------------------------------------ */

TEST_F(EnvPropertyFixture, SetEnvIfQueryStringMatch)
{
    session_.set_query_string("debug=1&verbose=true");

    auto *dir = make_setenvif("is_debug", "yes",
                               "Query_String", "debug=1");
    int rc = exec_setenvif(session_.handle(), dir);
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_TRUE(session_.has_env_var("is_debug"));
    EXPECT_EQ(session_.get_env_var("is_debug"), "yes");
    free_dir(dir);
}

TEST_F(EnvPropertyFixture, SetEnvIfQueryStringNoMatch)
{
    session_.set_query_string("page=2&sort=name");

    auto *dir = make_setenvif("is_debug", "yes",
                               "Query_String", "debug=1");
    int rc = exec_setenvif(session_.handle(), dir);
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_FALSE(session_.has_env_var("is_debug"));
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  SetEnvIf Request_Method attribute                                  */
/* ------------------------------------------------------------------ */

TEST_F(EnvPropertyFixture, SetEnvIfRequestMethodMatch)
{
    session_.set_method("POST");

    auto *dir = make_setenvif("is_post", "1",
                               "Request_Method", "^POST$");
    int rc = exec_setenvif(session_.handle(), dir);
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_TRUE(session_.has_env_var("is_post"));
    EXPECT_EQ(session_.get_env_var("is_post"), "1");
    free_dir(dir);
}

TEST_F(EnvPropertyFixture, SetEnvIfRequestMethodNoMatch)
{
    session_.set_method("GET");

    auto *dir = make_setenvif("is_post", "1",
                               "Request_Method", "^POST$");
    int rc = exec_setenvif(session_.handle(), dir);
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_FALSE(session_.has_env_var("is_post"));
    free_dir(dir);
}
