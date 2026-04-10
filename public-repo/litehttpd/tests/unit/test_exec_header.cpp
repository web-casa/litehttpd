/**
 * test_exec_header.cpp - Unit tests for Header/RequestHeader executors
 *
 * Tests specific examples for each operation and edge cases.
 *
 * Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_header.h"
#include "htaccess_directive.h"
#include "htaccess_parser.h"
#include "htaccess_printer.h"
}

/* ------------------------------------------------------------------ */
/*  Helper                                                             */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_dir(directive_type_t type,
                                      const char *name,
                                      const char *value = nullptr)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = type;
    d->line_number = 1;
    d->name = name ? strdup(name) : nullptr;
    d->value = value ? strdup(value) : nullptr;
    d->next = nullptr;
    return d;
}

static void free_dir(htaccess_directive_t *d)
{
    if (!d) return;
    free(d->name);
    free(d->value);
    free(d);
}

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class ExecHeaderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
        exec_header_reset_shadow();
    }

    MockSession session_;
};

/* ------------------------------------------------------------------ */
/*  Header set tests                                                   */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, HeaderSetBasic)
{
    auto *dir = make_dir(DIR_HEADER_SET, "X-Frame-Options", "DENY");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-Frame-Options"), "DENY");
    EXPECT_EQ(session_.count_response_headers("X-Frame-Options"), 1);
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderSetReplacesExisting)
{
    session_.add_response_header("X-Custom", "old-value");
    auto *dir = make_dir(DIR_HEADER_SET, "X-Custom", "new-value");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-Custom"), "new-value");
    EXPECT_EQ(session_.count_response_headers("X-Custom"), 1);
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderSetNullValueReturnsError)
{
    auto *dir = make_dir(DIR_HEADER_SET, "X-Test");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_ERROR);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Header unset tests                                                 */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, HeaderUnsetRemoves)
{
    session_.add_response_header("X-Powered-By", "PHP/8.0");
    auto *dir = make_dir(DIR_HEADER_UNSET, "X-Powered-By");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_FALSE(session_.has_response_header("X-Powered-By"));
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderUnsetNonExistentIsOk)
{
    auto *dir = make_dir(DIR_HEADER_UNSET, "X-NonExistent");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Header append tests                                                */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, HeaderAppendToExisting)
{
    session_.add_response_header("Cache-Control", "no-cache");
    auto *dir = make_dir(DIR_HEADER_APPEND, "Cache-Control", "no-store");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("Cache-Control"), "no-cache, no-store");
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderAppendToEmpty)
{
    auto *dir = make_dir(DIR_HEADER_APPEND, "X-New", "value1");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-New"), "value1");
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Header merge tests                                                 */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, HeaderMergeNewValue)
{
    session_.add_response_header("Vary", "Accept");
    auto *dir = make_dir(DIR_HEADER_MERGE, "Vary", "Accept-Encoding");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("Vary"), "Accept, Accept-Encoding");
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderMergeDuplicateSkipped)
{
    session_.add_response_header("Vary", "Accept");
    auto *dir = make_dir(DIR_HEADER_MERGE, "Vary", "Accept");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    /* Value should remain unchanged */
    EXPECT_EQ(session_.get_response_header("Vary"), "Accept");
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderMergeIdempotent)
{
    auto *dir = make_dir(DIR_HEADER_MERGE, "Vary", "Accept-Encoding");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    std::string first = session_.get_response_header("Vary");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    std::string second = session_.get_response_header("Vary");
    EXPECT_EQ(first, second);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Header add tests                                                   */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, HeaderAddCreatesNew)
{
    auto *dir = make_dir(DIR_HEADER_ADD, "Set-Cookie", "id=abc");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.count_response_headers("Set-Cookie"), 1);
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderAddAccumulates)
{
    session_.add_response_header("Set-Cookie", "id=abc");
    auto *dir = make_dir(DIR_HEADER_ADD, "Set-Cookie", "lang=en");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.count_response_headers("Set-Cookie"), 2);
    auto all = session_.get_all_response_headers("Set-Cookie");
    EXPECT_EQ(all[0], "id=abc");
    EXPECT_EQ(all[1], "lang=en");
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  RequestHeader set tests                                            */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, RequestHeaderSetBasic)
{
    auto *dir = make_dir(DIR_REQUEST_HEADER_SET, "X-Forwarded-For", "10.0.0.1");
    EXPECT_EQ(exec_request_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_request_header("X-Forwarded-For"), "10.0.0.1");
    free_dir(dir);
}

TEST_F(ExecHeaderTest, RequestHeaderSetReplacesExisting)
{
    session_.add_request_header("Authorization", "Bearer old");
    auto *dir = make_dir(DIR_REQUEST_HEADER_SET, "Authorization", "Bearer new");
    EXPECT_EQ(exec_request_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_request_header("Authorization"), "Bearer new");
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  RequestHeader unset tests                                          */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, RequestHeaderUnsetRemoves)
{
    session_.add_request_header("X-Debug", "true");
    auto *dir = make_dir(DIR_REQUEST_HEADER_UNSET, "X-Debug");
    EXPECT_EQ(exec_request_header(session_.handle(), dir), LSI_OK);
    EXPECT_FALSE(session_.has_request_header("X-Debug"));
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Edge cases                                                         */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, NullSessionReturnsError)
{
    auto *dir = make_dir(DIR_HEADER_SET, "X-Test", "val");
    EXPECT_EQ(exec_header(nullptr, dir), LSI_ERROR);
    EXPECT_EQ(exec_request_header(nullptr, dir), LSI_ERROR);
    free_dir(dir);
}

TEST_F(ExecHeaderTest, NullDirectiveReturnsError)
{
    EXPECT_EQ(exec_header(session_.handle(), nullptr), LSI_ERROR);
    EXPECT_EQ(exec_request_header(session_.handle(), nullptr), LSI_ERROR);
}

TEST_F(ExecHeaderTest, NullNameReturnsError)
{
    auto *dir = make_dir(DIR_HEADER_SET, nullptr, "val");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_ERROR);
    free_dir(dir);
}

TEST_F(ExecHeaderTest, InvalidTypeReturnsError)
{
    /* Use a type that doesn't belong to header executor */
    auto *dir = make_dir(DIR_PHP_VALUE, "X-Test", "val");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_ERROR);
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderSetSpecialCharacters)
{
    auto *dir = make_dir(DIR_HEADER_SET, "Content-Security-Policy",
                         "default-src 'self'; script-src 'unsafe-inline'");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("Content-Security-Policy"),
              "default-src 'self'; script-src 'unsafe-inline'");
    free_dir(dir);
}

/* ================================================================== */
/*  Header always tests (v2)                                           */
/*  Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.5                   */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/*  Header always set tests                                            */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, HeaderAlwaysSetBasic)
{
    auto *dir = make_dir(DIR_HEADER_ALWAYS_SET, "Strict-Transport-Security",
                         "max-age=31536000");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("Strict-Transport-Security"),
              "max-age=31536000");
    EXPECT_EQ(session_.count_response_headers("Strict-Transport-Security"), 1);
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderAlwaysSetReplacesExisting)
{
    session_.add_response_header("X-Custom", "old");
    auto *dir = make_dir(DIR_HEADER_ALWAYS_SET, "X-Custom", "new");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-Custom"), "new");
    EXPECT_EQ(session_.count_response_headers("X-Custom"), 1);
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderAlwaysSetOnErrorResponse)
{
    /* Simulate a 403 error response */
    session_.set_status_code(403);
    auto *dir = make_dir(DIR_HEADER_ALWAYS_SET, "X-Content-Type-Options",
                         "nosniff");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-Content-Type-Options"), "nosniff");
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderAlwaysSetOn500Response)
{
    session_.set_status_code(500);
    auto *dir = make_dir(DIR_HEADER_ALWAYS_SET, "X-Frame-Options", "DENY");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-Frame-Options"), "DENY");
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderAlwaysSetNullValueReturnsError)
{
    auto *dir = make_dir(DIR_HEADER_ALWAYS_SET, "X-Test");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_ERROR);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Header always unset tests                                          */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, HeaderAlwaysUnsetRemoves)
{
    session_.add_response_header("X-Powered-By", "PHP/8.0");
    auto *dir = make_dir(DIR_HEADER_ALWAYS_UNSET, "X-Powered-By");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_FALSE(session_.has_response_header("X-Powered-By"));
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderAlwaysUnsetOnErrorResponse)
{
    session_.set_status_code(404);
    session_.add_response_header("Server", "Apache");
    auto *dir = make_dir(DIR_HEADER_ALWAYS_UNSET, "Server");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_FALSE(session_.has_response_header("Server"));
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Header always append tests                                         */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, HeaderAlwaysAppendToExisting)
{
    session_.add_response_header("Cache-Control", "no-cache");
    auto *dir = make_dir(DIR_HEADER_ALWAYS_APPEND, "Cache-Control", "no-store");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("Cache-Control"),
              "no-cache, no-store");
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderAlwaysAppendOnErrorResponse)
{
    session_.set_status_code(503);
    auto *dir = make_dir(DIR_HEADER_ALWAYS_APPEND, "X-Debug", "error-info");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-Debug"), "error-info");
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Header always merge tests                                          */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, HeaderAlwaysMergeNewValue)
{
    session_.add_response_header("Vary", "Accept");
    auto *dir = make_dir(DIR_HEADER_ALWAYS_MERGE, "Vary", "Accept-Encoding");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("Vary"),
              "Accept, Accept-Encoding");
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderAlwaysMergeDuplicateSkipped)
{
    session_.add_response_header("Vary", "Accept");
    auto *dir = make_dir(DIR_HEADER_ALWAYS_MERGE, "Vary", "Accept");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("Vary"), "Accept");
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderAlwaysMergeOnErrorResponse)
{
    session_.set_status_code(502);
    auto *dir = make_dir(DIR_HEADER_ALWAYS_MERGE, "X-Info", "gateway-error");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-Info"), "gateway-error");
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Header always add tests                                            */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, HeaderAlwaysAddCreatesNew)
{
    auto *dir = make_dir(DIR_HEADER_ALWAYS_ADD, "Set-Cookie", "id=abc");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.count_response_headers("Set-Cookie"), 1);
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderAlwaysAddAccumulates)
{
    session_.add_response_header("Set-Cookie", "id=abc");
    auto *dir = make_dir(DIR_HEADER_ALWAYS_ADD, "Set-Cookie", "lang=en");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.count_response_headers("Set-Cookie"), 2);
    auto all = session_.get_all_response_headers("Set-Cookie");
    EXPECT_EQ(all[0], "id=abc");
    EXPECT_EQ(all[1], "lang=en");
    free_dir(dir);
}

TEST_F(ExecHeaderTest, HeaderAlwaysAddOnErrorResponse)
{
    session_.set_status_code(500);
    auto *dir = make_dir(DIR_HEADER_ALWAYS_ADD, "X-Error-Info", "details");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.count_response_headers("X-Error-Info"), 1);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Header always parsing tests                                        */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, ParseHeaderAlwaysSet)
{
    const char *input = "Header always set X-Frame-Options DENY\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_ALWAYS_SET);
    EXPECT_STREQ(dirs->name, "X-Frame-Options");
    EXPECT_STREQ(dirs->value, "DENY");
    htaccess_directives_free(dirs);
}

TEST_F(ExecHeaderTest, ParseHeaderAlwaysUnset)
{
    const char *input = "Header always unset X-Powered-By\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_ALWAYS_UNSET);
    EXPECT_STREQ(dirs->name, "X-Powered-By");
    EXPECT_TRUE(dirs->value == nullptr);
    htaccess_directives_free(dirs);
}

TEST_F(ExecHeaderTest, ParseHeaderAlwaysAppend)
{
    const char *input = "Header always append Cache-Control no-store\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_ALWAYS_APPEND);
    EXPECT_STREQ(dirs->name, "Cache-Control");
    EXPECT_STREQ(dirs->value, "no-store");
    htaccess_directives_free(dirs);
}

TEST_F(ExecHeaderTest, ParseHeaderAlwaysMerge)
{
    const char *input = "Header always merge Vary Accept-Encoding\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_ALWAYS_MERGE);
    EXPECT_STREQ(dirs->name, "Vary");
    EXPECT_STREQ(dirs->value, "Accept-Encoding");
    htaccess_directives_free(dirs);
}

TEST_F(ExecHeaderTest, ParseHeaderAlwaysAdd)
{
    const char *input = "Header always add Set-Cookie id=abc\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_ALWAYS_ADD);
    EXPECT_STREQ(dirs->name, "Set-Cookie");
    EXPECT_STREQ(dirs->value, "id=abc");
    htaccess_directives_free(dirs);
}

TEST_F(ExecHeaderTest, ParseHeaderAlwaysRoundTrip)
{
    const char *input = "Header always set Strict-Transport-Security max-age=31536000\n";
    htaccess_directive_t *dirs1 = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs1, nullptr);

    char *printed = htaccess_print(dirs1);
    ASSERT_NE(printed, nullptr);

    htaccess_directive_t *dirs2 = htaccess_parse(printed, strlen(printed), "test");
    ASSERT_NE(dirs2, nullptr);

    EXPECT_EQ(dirs1->type, dirs2->type);
    EXPECT_STREQ(dirs1->name, dirs2->name);
    EXPECT_STREQ(dirs1->value, dirs2->value);

    htaccess_directives_free(dirs1);
    htaccess_directives_free(dirs2);
    free(printed);
}

TEST_F(ExecHeaderTest, RegularHeaderStillWorks)
{
    /* Ensure non-always Header directives still parse correctly */
    const char *input = "Header set X-Test value123\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_SET);
    EXPECT_STREQ(dirs->name, "X-Test");
    EXPECT_STREQ(dirs->value, "value123");
    htaccess_directives_free(dirs);
}

/* ================================================================== */
/*  Feature 2: Header env=VAR condition support                        */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/*  Parser tests for env= condition                                    */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, ParseHeaderSetWithEnvCondition)
{
    const char *input = "Header set X-Foo \"bar\" env=REDIRECT_HTTPS\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_SET);
    EXPECT_STREQ(dirs->name, "X-Foo");
    EXPECT_STREQ(dirs->value, "bar");
    EXPECT_STREQ(dirs->env_condition, "REDIRECT_HTTPS");
    htaccess_directives_free(dirs);
}

TEST_F(ExecHeaderTest, ParseHeaderUnsetWithEnvCondition)
{
    const char *input = "Header unset X-Powered-By env=PROD\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_UNSET);
    EXPECT_STREQ(dirs->name, "X-Powered-By");
    EXPECT_STREQ(dirs->env_condition, "PROD");
    htaccess_directives_free(dirs);
}

TEST_F(ExecHeaderTest, ParseHeaderWithNoEnvCondition)
{
    const char *input = "Header set X-Foo bar\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_SET);
    EXPECT_TRUE(dirs->env_condition == nullptr);
    htaccess_directives_free(dirs);
}

TEST_F(ExecHeaderTest, ParseHeaderAlwaysSetWithEnv)
{
    const char *input = "Header always set X-HSTS \"max-age=31536000\" env=HTTPS\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_ALWAYS_SET);
    EXPECT_STREQ(dirs->name, "X-HSTS");
    EXPECT_STREQ(dirs->value, "max-age=31536000");
    EXPECT_STREQ(dirs->env_condition, "HTTPS");
    htaccess_directives_free(dirs);
}

/* ------------------------------------------------------------------ */
/*  Execution tests for env= condition                                 */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, EnvConditionMetExecutes)
{
    /* Set the environment variable */
    session_.add_env_var("HTTPS", "on");

    auto *dir = make_dir(DIR_HEADER_SET, "X-Secure", "yes");
    dir->env_condition = strdup("HTTPS");

    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-Secure"), "yes");
    htaccess_directives_free(dir);
}

TEST_F(ExecHeaderTest, EnvConditionNotMetSkips)
{
    /* Do NOT set the environment variable */
    auto *dir = make_dir(DIR_HEADER_SET, "X-Secure", "yes");
    dir->env_condition = strdup("HTTPS");

    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    /* Header should NOT be set */
    EXPECT_FALSE(session_.has_response_header("X-Secure"));
    htaccess_directives_free(dir);
}

TEST_F(ExecHeaderTest, EnvConditionNegatedNotSet)
{
    /* !VAR means execute when VAR is NOT set */
    auto *dir = make_dir(DIR_HEADER_SET, "X-Debug", "true");
    dir->env_condition = strdup("!PROD");

    /* PROD is not set, so !PROD is true → should execute */
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-Debug"), "true");
    htaccess_directives_free(dir);
}

TEST_F(ExecHeaderTest, EnvConditionNegatedSet)
{
    /* !VAR means skip when VAR IS set */
    session_.add_env_var("PROD", "1");

    auto *dir = make_dir(DIR_HEADER_SET, "X-Debug", "true");
    dir->env_condition = strdup("!PROD");

    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    /* Header should NOT be set because PROD is set and condition is !PROD */
    EXPECT_FALSE(session_.has_response_header("X-Debug"));
    htaccess_directives_free(dir);
}

TEST_F(ExecHeaderTest, EnvConditionNoConditionAlwaysExecutes)
{
    auto *dir = make_dir(DIR_HEADER_SET, "X-Always", "yes");
    /* dir->env_condition is NULL by default from calloc */
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-Always"), "yes");
    htaccess_directives_free(dir);
}

TEST_F(ExecHeaderTest, EnvConditionUnsetWithEnv)
{
    session_.add_response_header("X-Powered-By", "PHP");
    session_.add_env_var("STRIP_HEADERS", "1");

    auto *dir = make_dir(DIR_HEADER_UNSET, "X-Powered-By");
    dir->env_condition = strdup("STRIP_HEADERS");

    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_FALSE(session_.has_response_header("X-Powered-By"));
    htaccess_directives_free(dir);
}

/* ================================================================== */
/*  Feature 3: Header edit/edit* operation                             */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/*  Parser tests for edit/edit*                                        */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, ParseHeaderEdit)
{
    const char *input = "Header edit Set-Cookie ^(.*)$ $1;HttpOnly\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_EDIT);
    EXPECT_STREQ(dirs->name, "Set-Cookie");
    EXPECT_STREQ(dirs->data.header_ext.edit_pattern, "^(.*)$");
    EXPECT_STREQ(dirs->value, "$1;HttpOnly");
    htaccess_directives_free(dirs);
}

TEST_F(ExecHeaderTest, ParseHeaderEditStar)
{
    const char *input = "Header edit* Set-Cookie ^(.*)$ $1;Secure\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_EDIT_STAR);
    EXPECT_STREQ(dirs->name, "Set-Cookie");
    EXPECT_STREQ(dirs->data.header_ext.edit_pattern, "^(.*)$");
    EXPECT_STREQ(dirs->value, "$1;Secure");
    htaccess_directives_free(dirs);
}

TEST_F(ExecHeaderTest, ParseHeaderAlwaysEdit)
{
    const char *input = "Header always edit X-Custom foo bar\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_ALWAYS_EDIT);
    EXPECT_STREQ(dirs->name, "X-Custom");
    EXPECT_STREQ(dirs->data.header_ext.edit_pattern, "foo");
    EXPECT_STREQ(dirs->value, "bar");
    htaccess_directives_free(dirs);
}

TEST_F(ExecHeaderTest, ParseHeaderAlwaysEditStar)
{
    const char *input = "Header always edit* X-Custom foo bar\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_ALWAYS_EDIT_STAR);
    EXPECT_STREQ(dirs->name, "X-Custom");
    EXPECT_STREQ(dirs->data.header_ext.edit_pattern, "foo");
    EXPECT_STREQ(dirs->value, "bar");
    htaccess_directives_free(dirs);
}

TEST_F(ExecHeaderTest, ParseHeaderEditWithEnvCondition)
{
    const char *input = "Header edit X-Val foo bar env=REWRITE\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_EDIT);
    EXPECT_STREQ(dirs->name, "X-Val");
    EXPECT_STREQ(dirs->data.header_ext.edit_pattern, "foo");
    EXPECT_STREQ(dirs->value, "bar");
    EXPECT_STREQ(dirs->env_condition, "REWRITE");
    htaccess_directives_free(dirs);
}

/* ------------------------------------------------------------------ */
/*  Execution tests for edit/edit*                                     */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_edit_dir(directive_type_t type,
                                           const char *name,
                                           const char *pattern,
                                           const char *replacement)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = type;
    d->line_number = 1;
    d->name = name ? strdup(name) : nullptr;
    d->value = replacement ? strdup(replacement) : nullptr;
    d->data.header_ext.edit_pattern = pattern ? strdup(pattern) : nullptr;
    d->next = nullptr;
    return d;
}

TEST_F(ExecHeaderTest, HeaderEditFirstMatch)
{
    session_.add_response_header("X-Val", "foo bar foo");

    auto *dir = make_edit_dir(DIR_HEADER_EDIT, "X-Val", "foo", "baz");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    /* Only first "foo" should be replaced */
    EXPECT_EQ(session_.get_response_header("X-Val"), "baz bar foo");
    htaccess_directives_free(dir);
}

TEST_F(ExecHeaderTest, HeaderEditStarAllMatches)
{
    session_.add_response_header("X-Val", "foo bar foo");

    auto *dir = make_edit_dir(DIR_HEADER_EDIT_STAR, "X-Val", "foo", "baz");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    /* All "foo" should be replaced */
    EXPECT_EQ(session_.get_response_header("X-Val"), "baz bar baz");
    htaccess_directives_free(dir);
}

TEST_F(ExecHeaderTest, HeaderEditNoMatchUnchanged)
{
    session_.add_response_header("X-Val", "hello world");

    auto *dir = make_edit_dir(DIR_HEADER_EDIT, "X-Val", "xyz", "replaced");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-Val"), "hello world");
    htaccess_directives_free(dir);
}

TEST_F(ExecHeaderTest, HeaderEditNonExistentHeaderOk)
{
    /* Header doesn't exist — edit should be a no-op */
    auto *dir = make_edit_dir(DIR_HEADER_EDIT, "X-Missing", "foo", "bar");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_FALSE(session_.has_response_header("X-Missing"));
    htaccess_directives_free(dir);
}

TEST_F(ExecHeaderTest, HeaderEditRegexPattern)
{
    session_.add_response_header("X-Val", "version-123");

    auto *dir = make_edit_dir(DIR_HEADER_EDIT, "X-Val", "[0-9]+", "XXX");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-Val"), "version-XXX");
    htaccess_directives_free(dir);
}

TEST_F(ExecHeaderTest, HeaderAlwaysEditWorks)
{
    session_.add_response_header("Server", "Apache/2.4.41");

    auto *dir = make_edit_dir(DIR_HEADER_ALWAYS_EDIT, "Server", "Apache/[0-9.]+", "WebServer");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("Server"), "WebServer");
    htaccess_directives_free(dir);
}

TEST_F(ExecHeaderTest, HeaderEditWithEnvConditionMet)
{
    session_.add_response_header("X-Val", "old");
    session_.add_env_var("DO_EDIT", "1");

    auto *dir = make_edit_dir(DIR_HEADER_EDIT, "X-Val", "old", "new");
    dir->env_condition = strdup("DO_EDIT");

    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-Val"), "new");
    htaccess_directives_free(dir);
}

TEST_F(ExecHeaderTest, HeaderEditWithEnvConditionNotMet)
{
    session_.add_response_header("X-Val", "old");
    /* DO_EDIT is NOT set */

    auto *dir = make_edit_dir(DIR_HEADER_EDIT, "X-Val", "old", "new");
    dir->env_condition = strdup("DO_EDIT");

    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    /* Header should remain unchanged */
    EXPECT_EQ(session_.get_response_header("X-Val"), "old");
    htaccess_directives_free(dir);
}

TEST_F(ExecHeaderTest, HeaderEditInvalidRegexReturnsError)
{
    session_.add_response_header("X-Val", "test");

    auto *dir = make_edit_dir(DIR_HEADER_EDIT, "X-Val", "[invalid", "replaced");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_ERROR);
    htaccess_directives_free(dir);
}

/* ------------------------------------------------------------------ */
/*  Printer tests for env= and edit                                    */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, PrinterHeaderSetWithEnv)
{
    const char *input = "Header set X-Foo bar env=HTTPS\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    char *printed = htaccess_print(dirs);
    ASSERT_NE(printed, nullptr);
    EXPECT_STREQ(printed, "Header set X-Foo bar env=HTTPS\n");
    htaccess_directives_free(dirs);
    free(printed);
}

TEST_F(ExecHeaderTest, PrinterHeaderUnsetWithEnv)
{
    const char *input = "Header unset X-Powered-By env=PROD\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    char *printed = htaccess_print(dirs);
    ASSERT_NE(printed, nullptr);
    EXPECT_STREQ(printed, "Header unset X-Powered-By env=PROD\n");
    htaccess_directives_free(dirs);
    free(printed);
}

TEST_F(ExecHeaderTest, PrinterHeaderEdit)
{
    const char *input = "Header edit X-Val foo bar\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    char *printed = htaccess_print(dirs);
    ASSERT_NE(printed, nullptr);
    EXPECT_STREQ(printed, "Header edit X-Val foo bar\n");
    htaccess_directives_free(dirs);
    free(printed);
}

TEST_F(ExecHeaderTest, PrinterHeaderAlwaysEditStar)
{
    const char *input = "Header always edit* X-Val foo bar\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    char *printed = htaccess_print(dirs);
    ASSERT_NE(printed, nullptr);
    EXPECT_STREQ(printed, "Header always edit* X-Val foo bar\n");
    htaccess_directives_free(dirs);
    free(printed);
}

TEST_F(ExecHeaderTest, PrinterHeaderEditWithEnv)
{
    const char *input = "Header edit X-Val foo bar env=REWRITE\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    char *printed = htaccess_print(dirs);
    ASSERT_NE(printed, nullptr);
    EXPECT_STREQ(printed, "Header edit X-Val foo bar env=REWRITE\n");
    htaccess_directives_free(dirs);
    free(printed);
}

/* ================================================================== */
/*  Review fix verification tests                                      */
/* ================================================================== */

/* Fix #1: edit overflow returns LSI_ERROR instead of truncated data */
TEST_F(ExecHeaderTest, HeaderEditOverflowReturnsError)
{
    /* Create a header value longer than 4096 to trigger overflow */
    std::string long_val(5000, 'A');
    session_.add_response_header("X-Long", long_val);

    /* Replace all 'A' with 'BB' — result would be 10000 chars, overflows 4096 */
    auto *dir = make_edit_dir(DIR_HEADER_EDIT_STAR, "X-Long", "A", "BB");
    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_ERROR);
    htaccess_directives_free(dir);
}

/* Fix #3: empty-value env variable is still considered "set" */
TEST_F(ExecHeaderTest, EnvConditionEmptyValueIsStillSet)
{
    /* SetEnv FOO (empty value) should count as "set" */
    session_.add_env_var("FOO", "");

    auto *dir = make_dir(DIR_HEADER_SET, "X-Test", "yes");
    dir->env_condition = strdup("FOO");

    EXPECT_EQ(exec_header(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_response_header("X-Test"), "yes");
    htaccess_directives_free(dir);
}

/* Fix #2a: value containing " env=" with spaces in var name should not be extracted */
TEST_F(ExecHeaderTest, ParseHeaderValueContainingEnvStringWithTrailingChars)
{
    /* env=FOO BAR has a space in the var name — should not extract as env condition */
    const char *input = "Header set X-Test \"some env=FOO BAR\"\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_HEADER_SET);
    /* The space in "FOO BAR" should cause env= to be rejected */
    EXPECT_TRUE(dirs->env_condition == nullptr);
    htaccess_directives_free(dirs);
}

/* ------------------------------------------------------------------ */
/*  CRLF injection rejection tests                                     */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, RequestHeaderSetRejectsCRLFInName)
{
    auto *dir = make_dir(DIR_REQUEST_HEADER_SET, "X-Evil\r\nInjected", "value");
    EXPECT_EQ(exec_request_header(session_.handle(), dir), LSI_ERROR);
    /* Verify no header was written */
    EXPECT_TRUE(session_.get_request_header("X-Evil\r\nInjected").empty());
    free_dir(dir);
}

TEST_F(ExecHeaderTest, RequestHeaderSetRejectsCRLFInValue)
{
    auto *dir = make_dir(DIR_REQUEST_HEADER_SET, "X-Normal", "val\r\nEvil: injected");
    EXPECT_EQ(exec_request_header(session_.handle(), dir), LSI_ERROR);
    /* Verify no header was written */
    EXPECT_TRUE(session_.get_request_header("X-Normal").empty());
    free_dir(dir);
}

TEST_F(ExecHeaderTest, RequestHeaderSetRejectsLFInName)
{
    auto *dir = make_dir(DIR_REQUEST_HEADER_SET, "X-Evil\nInjected", "value");
    EXPECT_EQ(exec_request_header(session_.handle(), dir), LSI_ERROR);
    EXPECT_TRUE(session_.get_request_header("X-Evil\nInjected").empty());
    free_dir(dir);
}

TEST_F(ExecHeaderTest, RequestHeaderUnsetRejectsCRLFInName)
{
    /* Pre-set a legitimate header to verify it is NOT removed */
    session_.add_request_header("X-Keep", "keep-value");
    auto *dir = make_dir(DIR_REQUEST_HEADER_UNSET, "X-Evil\r\n");
    EXPECT_EQ(exec_request_header(session_.handle(), dir), LSI_ERROR);
    /* Verify the legitimate header was not touched */
    EXPECT_EQ(session_.get_request_header("X-Keep"), "keep-value");
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Header Edit CRLF injection in replacement text                     */
/* ------------------------------------------------------------------ */

TEST_F(ExecHeaderTest, HeaderEditRejectsCRLFInReplacement)
{
    const char *input =
        "Header edit X-Val \"^(.*)$\" \"injected\\r\\nEvil: header\"\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    if (dirs && dirs->type == DIR_HEADER_EDIT) {
        session_.add_response_header("X-Val", "original");
        int rc = exec_header(session_.handle(), dirs);
        if (rc == LSI_OK) {
            std::string val = session_.get_response_header("X-Val");
            EXPECT_EQ(val.find('\r'), std::string::npos) << "CRLF leaked into header";
            EXPECT_EQ(val.find('\n'), std::string::npos) << "LF leaked into header";
        }
    }
    htaccess_directives_free(dirs);
}
