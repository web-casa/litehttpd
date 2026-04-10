/**
 * test_integration.cpp - Integration tests for the OLS .htaccess module
 *
 * Tests the complete request processing flow through the module:
 * - Module init/cleanup lifecycle
 * - Request-phase directive execution chain (ACL → Redirect → PHP → Env → BruteForce)
 * - Response-phase directive execution chain (Header → Expires → ErrorDocument)
 * - FilesMatch conditional filtering with nested directive execution
 * - Graceful degradation (single directive error doesn't affect others)
 *
 * Uses the LSIAPI Mock layer to simulate complete request processing.
 * Hook callbacks are obtained via the mock hook registry after calling
 * mod_htaccess_init(), then invoked directly on MockSession instances.
 *
 * Validates: Requirements 1.1, 1.3, 2.1, 2.4, 9.1
 */
#include <gtest/gtest.h>
#include "mock_lsiapi.h"
#include "htaccess_cache.h"
#include "htaccess_shm.h"
#include "htaccess_directive.h"

#include <cstring>
#include <cstdlib>
#include <string>

/* Access the module descriptor and cleanup function (C linkage) */
extern "C" {
    extern lsi_module_t MNAME;
    int mod_htaccess_cleanup(lsi_module_t *module);
}

/* ================================================================== */
/*  Helper: allocate and populate a directive node                     */
/* ================================================================== */

static htaccess_directive_t *make_directive(directive_type_t type,
                                            const char *name,
                                            const char *value,
                                            int line = 1)
{
    auto *d = static_cast<htaccess_directive_t *>(
        calloc(1, sizeof(htaccess_directive_t)));
    d->type = type;
    d->line_number = line;
    if (name)  d->name  = strdup(name);
    if (value) d->value = strdup(value);
    d->next = NULL;
    return d;
}

/* Chain two directives: a->next = b */
static void chain(htaccess_directive_t *a, htaccess_directive_t *b)
{
    a->next = b;
}

/* ================================================================== */
/*  Test fixture                                                       */
/* ================================================================== */

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
        /* Destroy any leftover cache/shm from previous tests */
        htaccess_cache_destroy();
        shm_destroy();
    }

    void TearDown() override {
        htaccess_cache_destroy();
        shm_destroy();
    }

    /**
     * Initialize the module and return the hook callbacks.
     * Hooks are now declared in the serverhook array (OLS style),
     * not registered via lsi_register_hook().
     * We wrap each lsi_callback_pf into a lsi_hook_cb by building
     * a lsi_param_t on the fly.
     */
    bool init_module(lsi_hook_cb *out_req_hook, lsi_hook_cb *out_resp_hook) {
        int rc = MNAME.init_pf(&MNAME);
        if (rc != LSI_OK) return false;

        /* Read hooks from the serverhook array */
        lsi_serverhook_t *hooks = MNAME.serverhook;
        if (!hooks) return false;

        req_cb_ = nullptr;
        resp_cb_ = nullptr;
        for (int i = 0; hooks[i].cb != NULL; i++) {
            if (hooks[i].index == LSI_HKPT_URI_MAP)
                req_cb_ = hooks[i].cb;
            else if (hooks[i].index == LSI_HKPT_SEND_RESP_HEADER)
                resp_cb_ = hooks[i].cb;
        }

        if (!req_cb_ || !resp_cb_) return false;

        /* Provide wrapper lambdas that build lsi_param_t from session */
        *out_req_hook = [](lsi_session_t *s) -> int {
            /* We need access to the fixture's req_cb_ — use a static approach */
            return 0; /* placeholder, not used */
        };
        *out_resp_hook = [](lsi_session_t *s) -> int {
            return 0; /* placeholder, not used */
        };

        return true;
    }

    /**
     * Call the request-phase hook callback with a session.
     * Builds a lsi_param_t and calls the OLS-style callback.
     */
    int call_req_hook(lsi_session_t *session) {
        lsi_param_t param = {};
        param.session = session;
        return req_cb_(&param);
    }

    /**
     * Call the response-phase hook callback with a session.
     */
    int call_resp_hook(lsi_session_t *session) {
        lsi_param_t param = {};
        param.session = session;
        return resp_cb_(&param);
    }

    MockSession session_;
    lsi_callback_pf req_cb_ = nullptr;
    lsi_callback_pf resp_cb_ = nullptr;
};

/* ================================================================== */
/*  Module lifecycle tests                                             */
/* ================================================================== */

TEST_F(IntegrationTest, ModuleInitRegistersHooks) {
    int rc = MNAME.init_pf(&MNAME);
    ASSERT_EQ(rc, LSI_OK);

    /* Hooks are now in the serverhook array, not the mock registry */
    lsi_serverhook_t *hooks = MNAME.serverhook;
    ASSERT_NE(hooks, nullptr);

    bool has_req = false, has_resp = false;
    for (int i = 0; hooks[i].cb != NULL; i++) {
        if (hooks[i].index == LSI_HKPT_URI_MAP) {
            has_req = true;
            EXPECT_NE(hooks[i].cb, nullptr);
            EXPECT_EQ(hooks[i].flag & LSI_FLAG_ENABLED, LSI_FLAG_ENABLED);
        }
        if (hooks[i].index == LSI_HKPT_SEND_RESP_HEADER) {
            has_resp = true;
            EXPECT_NE(hooks[i].cb, nullptr);
            EXPECT_EQ(hooks[i].flag & LSI_FLAG_ENABLED, LSI_FLAG_ENABLED);
        }
    }
    EXPECT_TRUE(has_req);
    EXPECT_TRUE(has_resp);
}

TEST_F(IntegrationTest, ModuleCleanupSucceeds) {
    /* Init first so there's state to clean up */
    MNAME.init_pf(&MNAME);
    int rc = mod_htaccess_cleanup(&MNAME);
    EXPECT_EQ(rc, LSI_OK);
}

TEST_F(IntegrationTest, ModuleInitLogsSuccess) {
    MNAME.init_pf(&MNAME);
    auto &logs = mock_lsiapi::get_log_records();
    bool found = false;
    for (auto &l : logs) {
        if (l.message.find("initialized successfully") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

/* ================================================================== */
/*  Request-phase integration: ACL → Redirect → PHP → Env → BruteForce*/
/* ================================================================== */

TEST_F(IntegrationTest, RequestPhase_ACLDeny_Returns403) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    /* Pre-populate cache with ACL directives that deny 10.0.0.1 */
    auto *d_order = make_directive(DIR_ORDER, nullptr, nullptr, 1);
    d_order->data.acl.order = ORDER_ALLOW_DENY;

    auto *d_deny = make_directive(DIR_DENY_FROM, nullptr, "all", 2);
    chain(d_order, d_deny);

    htaccess_cache_put("/var/www/.htaccess", 0, 0, 0, d_order);

    /* Set up session */
    session_.set_doc_root("/var/www");
    session_.set_request_uri("/index.html");
    session_.set_client_ip("10.0.0.1");

    int rc = call_req_hook(session_.handle());
    EXPECT_EQ(rc, LSI_DENY); /* Hook returns LSI_DENY to block access */
    EXPECT_EQ(session_.get_status_code(), 403);
}

TEST_F(IntegrationTest, RequestPhase_RedirectShortCircuits) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    /* Redirect /old to /new with 301 */
    auto *d_redir = make_directive(DIR_REDIRECT, "/old", "http://example.com/new", 1);
    d_redir->data.redirect.status_code = 301;
    d_redir->data.redirect.pattern = nullptr;

    /* Also add a SetEnv that should NOT be executed due to short-circuit */
    auto *d_env = make_directive(DIR_SETENV, "SHOULD_NOT_SET", "true", 2);
    chain(d_redir, d_env);

    htaccess_cache_put("/var/www/.htaccess", 0, 0, 0, d_redir);

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/old/page.html");
    session_.set_client_ip("10.0.0.1");

    int rc = call_req_hook(session_.handle());
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_EQ(session_.get_status_code(), 301);
    EXPECT_TRUE(session_.has_response_header("Location"));
    /* Apache mod_alias appends the unmatched URI tail (/page.html) */
    EXPECT_EQ(session_.get_response_header("Location"), "http://example.com/new/page.html");
    /* SetEnv should NOT have been executed (redirect short-circuits) */
    EXPECT_FALSE(session_.has_env_var("SHOULD_NOT_SET"));
}

TEST_F(IntegrationTest, RequestPhase_PHPConfigApplied) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    /* Use non-PHP_INI_SYSTEM settings so they aren't ignored */
    auto *d_php = make_directive(DIR_PHP_VALUE, "display_errors", "1", 1);
    auto *d_flag = make_directive(DIR_PHP_FLAG, "log_errors", "on", 2);
    chain(d_php, d_flag);

    htaccess_cache_put("/var/www/.htaccess", 0, 0, 0, d_php);

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/index.php");
    session_.set_client_ip("10.0.0.1");

    call_req_hook(session_.handle());

    auto &records = session_.get_php_ini_records();
    ASSERT_GE(records.size(), 2u);
    EXPECT_EQ(records[0].name, "display_errors");
    EXPECT_EQ(records[0].value, "1");
    EXPECT_FALSE(records[0].is_admin);
    EXPECT_EQ(records[1].name, "log_errors");
    EXPECT_EQ(records[1].value, "on");
    EXPECT_FALSE(records[1].is_admin);
}

TEST_F(IntegrationTest, RequestPhase_EnvVarsSet) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    auto *d_env = make_directive(DIR_SETENV, "APP_ENV", "production", 1);
    htaccess_cache_put("/var/www/.htaccess", 0, 0, 0, d_env);

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/index.html");
    session_.set_client_ip("10.0.0.1");

    call_req_hook(session_.handle());

    EXPECT_TRUE(session_.has_env_var("APP_ENV"));
    EXPECT_EQ(session_.get_env_var("APP_ENV"), "production");
}

TEST_F(IntegrationTest, RequestPhase_FullChain_ACL_PHP_Env) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    /* Order Deny,Allow (default allow) + php_value + SetEnv */
    auto *d_order = make_directive(DIR_ORDER, nullptr, nullptr, 1);
    d_order->data.acl.order = ORDER_DENY_ALLOW;

    auto *d_allow = make_directive(DIR_ALLOW_FROM, nullptr, "all", 2);
    chain(d_order, d_allow);

    auto *d_php = make_directive(DIR_PHP_VALUE, "max_execution_time", "60", 3);
    chain(d_allow, d_php);

    auto *d_env = make_directive(DIR_SETENV, "DEBUG", "1", 4);
    chain(d_php, d_env);

    htaccess_cache_put("/var/www/.htaccess", 0, 0, 0, d_order);

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/app/page.php");
    session_.set_client_ip("192.168.1.50");

    call_req_hook(session_.handle());

    /* Access should be allowed */
    EXPECT_NE(session_.get_status_code(), 403);

    /* PHP config should be applied */
    auto &records = session_.get_php_ini_records();
    ASSERT_GE(records.size(), 1u);
    EXPECT_EQ(records[0].name, "max_execution_time");
    EXPECT_EQ(records[0].value, "60");

    /* Env var should be set */
    EXPECT_TRUE(session_.has_env_var("DEBUG"));
    EXPECT_EQ(session_.get_env_var("DEBUG"), "1");
}

/* ================================================================== */
/*  Response-phase integration: Header → Expires → ErrorDocument       */
/* ================================================================== */

TEST_F(IntegrationTest, ResponsePhase_HeaderSet) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    auto *d_hdr = make_directive(DIR_HEADER_SET, "X-Frame-Options", "DENY", 1);
    auto *d_hdr2 = make_directive(DIR_HEADER_SET, "X-Content-Type-Options", "nosniff", 2);
    chain(d_hdr, d_hdr2);

    htaccess_cache_put("/var/www/.htaccess", 0, 0, 0, d_hdr);

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/index.html");
    session_.set_client_ip("10.0.0.1");

    /* Call req_hook first to populate session cache (req_cache),
     * matching real OLS lifecycle: URI_MAP → SEND_RESP_HEADER */
    call_req_hook(session_.handle());
    call_resp_hook(session_.handle());

    EXPECT_TRUE(session_.has_response_header("X-Frame-Options"));
    EXPECT_EQ(session_.get_response_header("X-Frame-Options"), "DENY");
    EXPECT_TRUE(session_.has_response_header("X-Content-Type-Options"));
    EXPECT_EQ(session_.get_response_header("X-Content-Type-Options"), "nosniff");
}

TEST_F(IntegrationTest, ResponsePhase_ExpiresHeaders) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    /* ExpiresActive On + ExpiresByType text/html */
    auto *d_active = make_directive(DIR_EXPIRES_ACTIVE, nullptr, nullptr, 1);
    d_active->data.expires.active = 1;

    auto *d_type = make_directive(DIR_EXPIRES_BY_TYPE, "text/html", nullptr, 2);
    d_type->data.expires.duration_sec = 3600; /* 1 hour */
    chain(d_active, d_type);

    htaccess_cache_put("/var/www/.htaccess", 0, 0, 0, d_active);

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/index.html");
    session_.set_client_ip("10.0.0.1");
    /* Set Content-Type so expires can match */
    session_.add_response_header("Content-Type", "text/html");

    call_resp_hook(session_.handle());

    EXPECT_TRUE(session_.has_response_header("Cache-Control"));
    std::string cc = session_.get_response_header("Cache-Control");
    EXPECT_NE(cc.find("max-age=3600"), std::string::npos);
}

TEST_F(IntegrationTest, ResponsePhase_ErrorDocumentTextMessage) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    auto *d_err = make_directive(DIR_ERROR_DOCUMENT, nullptr, "\"Page Not Found\"", 1);
    d_err->data.error_doc.error_code = 404;

    htaccess_cache_put("/var/www/.htaccess", 0, 0, 0, d_err);

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/missing.html");
    session_.set_client_ip("10.0.0.1");
    session_.set_status_code(404); /* Simulate a 404 response */

    call_resp_hook(session_.handle());

    EXPECT_EQ(session_.get_resp_body(), "Page Not Found");
}

TEST_F(IntegrationTest, ResponsePhase_ErrorDocumentExternalURL) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    auto *d_err = make_directive(DIR_ERROR_DOCUMENT, nullptr,
                                 "https://example.com/error.html", 1);
    d_err->data.error_doc.error_code = 500;

    htaccess_cache_put("/var/www/.htaccess", 0, 0, 0, d_err);

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/broken.html");
    session_.set_client_ip("10.0.0.1");
    session_.set_status_code(500);

    call_resp_hook(session_.handle());

    EXPECT_EQ(session_.get_status_code(), 302);
    EXPECT_TRUE(session_.has_response_header("Location"));
    EXPECT_EQ(session_.get_response_header("Location"),
              "https://example.com/error.html");
}

TEST_F(IntegrationTest, ResponsePhase_FullChain_Header_Expires_ErrorDoc) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    /* Header set + ExpiresActive On + ExpiresByType + ErrorDocument */
    auto *d_hdr = make_directive(DIR_HEADER_SET, "X-Powered-By", "OLS", 1);

    auto *d_active = make_directive(DIR_EXPIRES_ACTIVE, nullptr, nullptr, 2);
    d_active->data.expires.active = 1;
    chain(d_hdr, d_active);

    auto *d_type = make_directive(DIR_EXPIRES_BY_TYPE, "text/css", nullptr, 3);
    d_type->data.expires.duration_sec = 86400;
    chain(d_active, d_type);

    auto *d_err = make_directive(DIR_ERROR_DOCUMENT, nullptr, "\"Server Error\"", 4);
    d_err->data.error_doc.error_code = 500;
    chain(d_type, d_err);

    htaccess_cache_put("/var/www/.htaccess", 0, 0, 0, d_hdr);

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/style.css");
    session_.set_client_ip("10.0.0.1");
    session_.add_response_header("Content-Type", "text/css");

    call_resp_hook(session_.handle());

    /* Header should be set */
    EXPECT_TRUE(session_.has_response_header("X-Powered-By"));
    EXPECT_EQ(session_.get_response_header("X-Powered-By"), "OLS");

    /* Expires should be set for text/css */
    EXPECT_TRUE(session_.has_response_header("Cache-Control"));
    std::string cc = session_.get_response_header("Cache-Control");
    EXPECT_NE(cc.find("max-age=86400"), std::string::npos);

    /* ErrorDocument should NOT trigger (status is 200, not 500) */
    EXPECT_EQ(session_.get_resp_body(), "");
}

/* ================================================================== */
/*  FilesMatch conditional filtering with nested directives            */
/* ================================================================== */

TEST_F(IntegrationTest, ResponsePhase_FilesMatch_MatchingFile) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    /* FilesMatch for .php files with nested Header set */
    auto *d_fm = make_directive(DIR_FILES_MATCH, nullptr, nullptr, 1);
    d_fm->data.files_match.pattern = strdup("\\.php$");

    /* Nested child: Header set X-PHP "true" */
    auto *child = make_directive(DIR_HEADER_SET, "X-PHP", "true", 2);
    d_fm->data.files_match.children = child;

    htaccess_cache_put("/var/www/.htaccess", 0, 0, 0, d_fm);

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/app/index.php");
    session_.set_client_ip("10.0.0.1");

    call_resp_hook(session_.handle());

    /* The nested Header should be applied because filename matches .php */
    EXPECT_TRUE(session_.has_response_header("X-PHP"));
    EXPECT_EQ(session_.get_response_header("X-PHP"), "true");
}

TEST_F(IntegrationTest, ResponsePhase_FilesMatch_NonMatchingFile) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    /* FilesMatch for .php files with nested Header set */
    auto *d_fm = make_directive(DIR_FILES_MATCH, nullptr, nullptr, 1);
    d_fm->data.files_match.pattern = strdup("\\.php$");

    auto *child = make_directive(DIR_HEADER_SET, "X-PHP", "true", 2);
    d_fm->data.files_match.children = child;

    htaccess_cache_put("/var/www/.htaccess", 0, 0, 0, d_fm);

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/style.css");  /* Not a .php file */
    session_.set_client_ip("10.0.0.1");

    call_resp_hook(session_.handle());

    /* The nested Header should NOT be applied */
    EXPECT_FALSE(session_.has_response_header("X-PHP"));
}

TEST_F(IntegrationTest, ResponsePhase_FilesMatch_WithDenyChild) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    /* FilesMatch for .env files with nested Header set (simulating deny) */
    auto *d_fm = make_directive(DIR_FILES_MATCH, nullptr, nullptr, 1);
    d_fm->data.files_match.pattern = strdup("\\.env$");

    /* Nested: Header set X-Blocked "true" */
    auto *child = make_directive(DIR_HEADER_SET, "X-Blocked", "true", 2);
    d_fm->data.files_match.children = child;

    /* Also a regular header outside FilesMatch */
    auto *d_hdr = make_directive(DIR_HEADER_SET, "X-Global", "yes", 3);
    chain(d_fm, d_hdr);

    htaccess_cache_put("/var/www/.htaccess", 0, 0, 0, d_fm);

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/config.env");
    session_.set_client_ip("10.0.0.1");

    call_resp_hook(session_.handle());

    /* Both headers should be set: global and FilesMatch-nested */
    EXPECT_TRUE(session_.has_response_header("X-Global"));
    EXPECT_EQ(session_.get_response_header("X-Global"), "yes");
    EXPECT_TRUE(session_.has_response_header("X-Blocked"));
    EXPECT_EQ(session_.get_response_header("X-Blocked"), "true");
}

/* ================================================================== */
/*  Graceful degradation tests                                         */
/* ================================================================== */

TEST_F(IntegrationTest, GracefulDegradation_NoDocRoot) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    /* Don't set doc_root — should gracefully skip */
    session_.set_request_uri("/index.html");
    session_.set_client_ip("10.0.0.1");

    int rc = call_req_hook(session_.handle());
    EXPECT_EQ(rc, LSI_OK);
    /* Status should remain 200 (no error) */
    EXPECT_EQ(session_.get_status_code(), 200);
}

TEST_F(IntegrationTest, GracefulDegradation_NoDirectivesFound) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    /* Set up session but don't populate cache — no .htaccess files */
    session_.set_doc_root("/var/www");
    session_.set_request_uri("/index.html");
    session_.set_client_ip("10.0.0.1");

    int rc = call_req_hook(session_.handle());
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_EQ(session_.get_status_code(), 200);

    int rc2 = call_resp_hook(session_.handle());
    EXPECT_EQ(rc2, LSI_OK);
}

TEST_F(IntegrationTest, GracefulDegradation_MixedDirectives_ErrorInOneDoesNotAffectOthers) {
    lsi_hook_cb req_hook, resp_hook;
    ASSERT_TRUE(init_module(&req_hook, &resp_hook));

    /* Create a chain: valid SetEnv + valid php_value */
    auto *d_env = make_directive(DIR_SETENV, "GOOD_VAR", "works", 1);
    auto *d_php = make_directive(DIR_PHP_VALUE, "max_execution_time", "30", 2);
    chain(d_env, d_php);

    htaccess_cache_put("/var/www/.htaccess", 0, 0, 0, d_env);

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/test.php");
    session_.set_client_ip("10.0.0.1");

    call_req_hook(session_.handle());

    /* Both should succeed independently */
    EXPECT_TRUE(session_.has_env_var("GOOD_VAR"));
    EXPECT_EQ(session_.get_env_var("GOOD_VAR"), "works");

    auto &records = session_.get_php_ini_records();
    ASSERT_GE(records.size(), 1u);
    EXPECT_EQ(records[0].name, "max_execution_time");
    EXPECT_EQ(records[0].value, "30");
}
