/**
 * test_shim_semantics.cpp — Documents known mock vs shim behavior differences
 *
 * These tests verify that mock behavior is DOCUMENTED and consistent,
 * even where it diverges from production shim behavior. Each test
 * documents what the mock does vs what the shim does in production.
 *
 * Production divergences (shim limitations):
 *   - RequestHeader: mock modifies req_headers_, shim writes HTTP_* env
 *   - Options: mock stores dir_options_, shim writes DIR_OPT_* env hint
 *   - AddHandler/SetHandler: mock returns LSI_OK, shim is no-op with DEBUG log
 *   - php_value on stock OLS: mock records, shim env-var doesn't reach lsphp
 */
#include <gtest/gtest.h>
#include <cstring>
#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_directive.h"
#include "htaccess_exec_header.h"
#include "htaccess_exec_options.h"
#include "htaccess_exec_handler.h"
#include "htaccess_exec_php.h"
#include "htaccess_exec_error_doc.h"
}

class ShimSemanticsTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
    MockSession session_;
};

/* ================================================================== */
/*  RequestHeader — mock changes request header map                     */
/*  In production: shim writes HTTP_* env var instead                   */
/* ================================================================== */

TEST_F(ShimSemanticsTest, RequestHeaderSetMockBehavior) {
    /* Mock: directly sets request header (visible to has_request_header) */
    lsi_session_set_req_header(session_.handle(),
                               "X-Forwarded-Proto", 17, "https", 5);
    EXPECT_TRUE(session_.has_request_header("X-Forwarded-Proto"));
    EXPECT_EQ(session_.get_request_header("X-Forwarded-Proto"), "https");
    /* NOTE: In production shim, this writes HTTP_X_FORWARDED_PROTO env var
     * instead. Backend reads via $_SERVER['HTTP_X_FORWARDED_PROTO']. */
}

TEST_F(ShimSemanticsTest, RequestHeaderUnsetMockBehavior) {
    session_.add_request_header("X-Remove", "value");
    lsi_session_remove_req_header(session_.handle(), "X-Remove", 8);
    EXPECT_FALSE(session_.has_request_header("X-Remove"));
    /* NOTE: In production, shim sets HTTP_X_REMOVE="" (empty, not removed) */
}

/* ================================================================== */
/*  Options — mock stores state, shim writes env hint                  */
/* ================================================================== */

TEST_F(ShimSemanticsTest, OptionsMockBehavior) {
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_OPTIONS;
    d->data.options.indexes = -1;  /* -Indexes */
    d->data.options.follow_symlinks = 1;  /* +FollowSymLinks */

    exec_options(session_.handle(), d);

    EXPECT_EQ(session_.get_dir_option("Indexes"), 0);
    EXPECT_EQ(session_.get_dir_option("FollowSymLinks"), 1);
    /* NOTE: In production, shim sets DIR_OPT_Indexes=0 and
     * DIR_OPT_FollowSymLinks=1 as env vars. OLS doesn't actually
     * change directory behavior based on these hints. */

    free(d);
}

/* ================================================================== */
/*  AddHandler/SetHandler — both mock and shim are no-op               */
/* ================================================================== */

TEST_F(ShimSemanticsTest, HandlerIsNoOp) {
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ADD_HANDLER;
    d->name = strdup("application/x-httpd-php");
    d->value = strdup(".php");

    int rc = exec_add_handler(session_.handle(), d);
    EXPECT_EQ(rc, LSI_OK);
    /* Both mock and shim return LSI_OK without any side effects.
     * OLS handles handler mapping via scriptHandler in vhconf. */

    htaccess_directives_free(d);
}

/* ================================================================== */
/*  php_value — mock records, shim env-var may not reach lsphp         */
/* ================================================================== */

TEST_F(ShimSemanticsTest, PhpValueMockRecords) {
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_PHP_VALUE;
    d->name = strdup("upload_max_filesize");
    d->value = strdup("128M");
    d->line_number = 1;

    exec_php_value(session_.handle(), d);

    auto &records = session_.get_php_ini_records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].name, "upload_max_filesize");
    EXPECT_EQ(records[0].value, "128M");
    /* NOTE: In production on stock OLS, the env-var fallback
     * (PHP_VALUE=upload_max_filesize=128M) does NOT reach lsphp.
     * Requires custom OLS with PHPConfig patch for this to work. */

    htaccess_directives_free(d);
}

/* ================================================================== */
/*  ErrorDocument local path — mock sets env, HTTP_BEGIN does redirect  */
/* ================================================================== */

TEST_F(ShimSemanticsTest, ErrorDocLocalPathMockBehavior) {
    session_.set_status_code(404);

    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ERROR_DOCUMENT;
    d->value = strdup("/errors/404.html");
    d->data.error_doc.error_code = 404;

    /* In response hook: sets env hint */
    exec_error_document(session_.handle(), d);

    EXPECT_TRUE(session_.has_env_var("OLS_ERROR_DOC_URI"));
    EXPECT_EQ(session_.get_env_var("OLS_ERROR_DOC_URI"), "/errors/404.html");
    /* NOTE: The actual internal redirect happens in HTTP_BEGIN hook
     * (on_http_begin), not in the response phase. Unit tests can't
     * verify the HTTP_BEGIN behavior because init_module only extracts
     * URI_MAP and SEND_RESP_HEADER callbacks. */

    htaccess_directives_free(d);
}
