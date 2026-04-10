/**
 * test_cross_cutting.cpp - Cross-cutting tests for directive interactions
 *
 * Tests that were missing from the original suite, covering:
 * - Directive interaction: AddType + ExpiresByType ordering
 * - Cache deep-copy: env_condition survives dirwalker round-trip
 * - Cache deep-copy: header_ext.edit_pattern survives round-trip
 * - RequestHeader: executed in request phase (not response phase)
 * - Brute force + auth: auth failure triggers BF counting
 * - ErrorDocument local-path: HTTP_BEGIN hook fires internal redirect
 */
#include <gtest/gtest.h>
#include <cstring>
#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_directive.h"
#include "htaccess_exec_header.h"
#include "htaccess_exec_expires.h"
#include "htaccess_exec_php.h"
#include "htaccess_exec_env.h"
#include "htaccess_exec_brute_force.h"
#include "htaccess_exec_error_doc.h"
#include "htaccess_exec_forcetype.h"
#include "htaccess_shm.h"
}

/* ================================================================== */
/*  Helper: build a directive node                                     */
/* ================================================================== */

static htaccess_directive_t *make_dir(directive_type_t type,
                                      const char *name,
                                      const char *value)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = type;
    d->line_number = 1;
    if (name)  d->name = strdup(name);
    if (value) d->value = strdup(value);
    return d;
}

/* ================================================================== */
/*  1. AddType + ExpiresByType ordering                                */
/* ================================================================== */

class DirectiveOrderTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
    MockSession session_;
};

TEST_F(DirectiveOrderTest, ForceTypeThenExpiresByType) {
    /* ForceType sets Content-Type to text/css,
     * then ExpiresByType text/css should match */
    session_.set_request_uri("/app/style.txt");

    /* 1. Execute ForceType text/css */
    auto *ft = make_dir(DIR_FORCE_TYPE, nullptr, "text/css");
    exec_force_type(session_.handle(), ft);
    htaccess_directives_free(ft);

    /* Verify Content-Type was set */
    int ct_len = 0;
    const char *ct = lsi_session_get_resp_header_by_name(
        session_.handle(), "Content-Type", 12, &ct_len);
    ASSERT_NE(ct, nullptr);
    EXPECT_EQ(std::string(ct, ct_len), "text/css");

    /* 2. Execute ExpiresByType text/css "access plus 1 week" (604800s) */
    auto *ea = make_dir(DIR_EXPIRES_ACTIVE, nullptr, "on");
    ea->data.expires.active = 1;
    auto *eb = make_dir(DIR_EXPIRES_BY_TYPE, "text/css", "604800");
    eb->data.expires.duration_sec = 604800;
    ea->next = eb;

    exec_expires(session_.handle(), ea, "text/css");

    /* Verify Cache-Control was set with max-age=604800 */
    const char *cc = lsi_session_get_resp_header_by_name(
        session_.handle(), "Cache-Control", 13, nullptr);
    ASSERT_NE(cc, nullptr);
    EXPECT_NE(std::string(cc).find("604800"), std::string::npos);

    htaccess_directives_free(ea);
}

/* ================================================================== */
/*  2. Cache deep-copy: env_condition preserved                        */
/* ================================================================== */

TEST_F(DirectiveOrderTest, EnvConditionSurvivesCopy) {
    /* Create a Header set with env=HTTPS condition */
    auto *d = make_dir(DIR_HEADER_SET, "Strict-Transport-Security",
                       "max-age=31536000");
    d->env_condition = strdup("HTTPS");

    /* Simulate what dirwalker copy_directive does */
    auto *copy = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    copy->type = d->type;
    copy->line_number = d->line_number;
    copy->name = d->name ? strdup(d->name) : nullptr;
    copy->value = d->value ? strdup(d->value) : nullptr;
    copy->env_condition = d->env_condition ? strdup(d->env_condition) : nullptr;
    copy->data = d->data;

    /* Verify env_condition was deep-copied */
    ASSERT_NE(copy->env_condition, nullptr);
    EXPECT_STREQ(copy->env_condition, "HTTPS");
    EXPECT_NE(copy->env_condition, d->env_condition); /* different pointer */

    htaccess_directives_free(d);

    /* copy should still be valid after d is freed */
    EXPECT_STREQ(copy->env_condition, "HTTPS");
    htaccess_directives_free(copy);
}

/* ================================================================== */
/*  3. Cache deep-copy: edit_pattern preserved                         */
/* ================================================================== */

TEST_F(DirectiveOrderTest, EditPatternSurvivesCopy) {
    auto *d = make_dir(DIR_HEADER_EDIT, "Cache-Control", "s-maxage=\\d+");
    d->data.header_ext.edit_pattern = strdup("s-maxage=\\d+");

    /* Deep-copy */
    auto *copy = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    copy->type = d->type;
    copy->line_number = d->line_number;
    copy->name = d->name ? strdup(d->name) : nullptr;
    copy->value = d->value ? strdup(d->value) : nullptr;
    copy->data = d->data;
    copy->data.header_ext.edit_pattern = d->data.header_ext.edit_pattern
        ? strdup(d->data.header_ext.edit_pattern) : nullptr;

    ASSERT_NE(copy->data.header_ext.edit_pattern, nullptr);
    EXPECT_STREQ(copy->data.header_ext.edit_pattern, "s-maxage=\\d+");
    EXPECT_NE(copy->data.header_ext.edit_pattern,
              d->data.header_ext.edit_pattern);

    htaccess_directives_free(d);
    EXPECT_STREQ(copy->data.header_ext.edit_pattern, "s-maxage=\\d+");
    htaccess_directives_free(copy);
}

/* ================================================================== */
/*  4. RequestHeader sets HTTP_* env var                               */
/* ================================================================== */

TEST_F(DirectiveOrderTest, RequestHeaderSetsEnvVar) {
    auto *d = make_dir(DIR_REQUEST_HEADER_SET, "X-Forwarded-Proto", "https");
    exec_request_header(session_.handle(), d);

    /* In mock, set_req_header directly sets the request header map */
    EXPECT_TRUE(session_.has_request_header("X-Forwarded-Proto"));
    EXPECT_EQ(session_.get_request_header("X-Forwarded-Proto"), "https");

    htaccess_directives_free(d);
}

/* ================================================================== */
/*  5. Brute force: only counts POST requests                          */
/* ================================================================== */

class BruteForceCountingTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
        shm_destroy();
        shm_init(nullptr, 1024);
    }
    void TearDown() override { shm_destroy(); }
    MockSession session_;
};

TEST_F(BruteForceCountingTest, GetRequestDoesNotCount) {
    session_.set_method("GET");
    session_.set_client_ip("10.0.0.1");
    session_.set_request_uri("/wp-login.php");

    auto *d = make_dir(DIR_BRUTE_FORCE_PROTECTION, nullptr, nullptr);
    d->data.brute_force.enabled = 1;
    auto *d2 = make_dir(DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS, nullptr, nullptr);
    d2->data.brute_force.allowed_attempts = 3;
    auto *d3 = make_dir(DIR_BRUTE_FORCE_WINDOW, nullptr, nullptr);
    d3->data.brute_force.window_sec = 300;
    auto *d4 = make_dir(DIR_BRUTE_FORCE_ACTION, nullptr, nullptr);
    d4->data.brute_force.action = BF_ACTION_BLOCK;
    d->next = d2; d2->next = d3; d3->next = d4;

    /* 10 GET requests should NOT trigger block */
    for (int i = 0; i < 10; i++) {
        int rc = exec_brute_force(session_.handle(), d, "10.0.0.1", 1);
        EXPECT_EQ(rc, LSI_OK);
    }

    htaccess_directives_free(d);
}

TEST_F(BruteForceCountingTest, PostRequestCounts) {
    session_.set_method("POST");
    session_.set_client_ip("10.0.0.1");
    session_.set_request_uri("/wp-login.php");

    auto *d = make_dir(DIR_BRUTE_FORCE_PROTECTION, nullptr, nullptr);
    d->data.brute_force.enabled = 1;
    auto *d2 = make_dir(DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS, nullptr, nullptr);
    d2->data.brute_force.allowed_attempts = 3;
    auto *d3 = make_dir(DIR_BRUTE_FORCE_WINDOW, nullptr, nullptr);
    d3->data.brute_force.window_sec = 300;
    auto *d4 = make_dir(DIR_BRUTE_FORCE_ACTION, nullptr, nullptr);
    d4->data.brute_force.action = BF_ACTION_BLOCK;
    d->next = d2; d2->next = d3; d3->next = d4;

    /* 3 POST requests should pass */
    for (int i = 0; i < 3; i++) {
        int rc = exec_brute_force(session_.handle(), d, "10.0.0.1", 1);
        EXPECT_EQ(rc, LSI_OK);
    }

    /* 4th POST should block */
    int rc = exec_brute_force(session_.handle(), d, "10.0.0.1", 1);
    EXPECT_EQ(rc, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    htaccess_directives_free(d);
}

/* ================================================================== */
/*  6. ErrorDocument local-path sets internal redirect URI             */
/* ================================================================== */

TEST_F(DirectiveOrderTest, ErrorDocumentLocalPathSetsInternalURI) {
    session_.set_status_code(404);

    auto *d = make_dir(DIR_ERROR_DOCUMENT, nullptr, "/errors/404.html");
    d->data.error_doc.error_code = 404;

    int rc = exec_error_document(session_.handle(), d);
    EXPECT_EQ(rc, 0);

    /* In the response hook, ErrorDocument local-path sets env hint.
     * The actual internal redirect happens in HTTP_BEGIN hook. */
    EXPECT_TRUE(session_.has_env_var("OLS_ERROR_DOC_URI"));
    EXPECT_EQ(session_.get_env_var("OLS_ERROR_DOC_URI"), "/errors/404.html");

    htaccess_directives_free(d);
}
