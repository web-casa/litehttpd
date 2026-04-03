/**
 * test_container_semantics.cpp - Container/inheritance/precedence matrix tests
 *
 * Tests the full hook execution chain for:
 * - Require vs legacy ACL precedence
 * - <Files>/<FilesMatch> container ACL
 * - <Limit>/<LimitExcept> method restriction
 * - Directory inheritance (parent/child override)
 */
#include <gtest/gtest.h>
#include <cstring>
#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_directive.h"
#include "htaccess_dirwalker.h"
#include "htaccess_cache.h"
#include "htaccess_shm.h"
#include "htaccess_exec_acl.h"
#include "htaccess_exec_require.h"
#include "htaccess_exec_expires.h"
#include "htaccess_exec_header.h"
#include "htaccess_exec_dirindex.h"
#include "htaccess_parser.h"

/* Access the module descriptor */
extern lsi_module_t MNAME;
extern int mod_htaccess_cleanup(lsi_module_t *);
}

/* ================================================================== */
/*  Test fixture with full hook chain                                   */
/* ================================================================== */

class ContainerSemanticsTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
        htaccess_cache_init(64);
        shm_init(nullptr, 1024);
        MNAME.init_pf(&MNAME);

        /* Find URI_MAP and SEND_RESP_HEADER callbacks */
        lsi_serverhook_t *hooks = MNAME.serverhook;
        req_cb_ = nullptr;
        resp_cb_ = nullptr;
        for (int i = 0; hooks && hooks[i].cb != NULL; i++) {
            if (hooks[i].index == LSI_HKPT_URI_MAP)
                req_cb_ = hooks[i].cb;
            else if (hooks[i].index == LSI_HKPT_SEND_RESP_HEADER)
                resp_cb_ = hooks[i].cb;
        }
    }

    void TearDown() override {
        htaccess_cache_destroy();
        shm_destroy();
    }

    int call_req_hook() {
        if (!req_cb_) return -1;
        lsi_param_t param = {};
        param.session = session_.handle();
        return req_cb_(&param);
    }

    int call_resp_hook() {
        if (!resp_cb_) return -1;
        lsi_param_t param = {};
        param.session = session_.handle();
        return resp_cb_(&param);
    }

    void setup_htaccess(const char *path, const char *content) {
        auto *dirs = htaccess_parse(content, strlen(content), path);
        if (dirs)
            htaccess_cache_put(path, 0, 0, 0, dirs);
    }

    MockSession session_;
    lsi_callback_pf req_cb_ = nullptr;
    lsi_callback_pf resp_cb_ = nullptr;
};

/* ================================================================== */
/*  Require precedence over legacy ACL                                 */
/* ================================================================== */

TEST_F(ContainerSemanticsTest, RequireTakesPrecedenceOverDeny) {
    /* Deny from all + Require all granted → should ALLOW (Require wins) */
    setup_htaccess("/var/www/.htaccess",
        "Order Allow,Deny\n"
        "Deny from all\n"
        "Require all granted\n");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/index.html");
    session_.set_client_ip("10.0.0.1");

    int rc = call_req_hook();
    /* Should NOT be denied — Require all granted takes precedence */
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_NE(session_.get_status_code(), 403);
}

TEST_F(ContainerSemanticsTest, RequireAllDeniedOverridesAllow) {
    /* Allow from all + Require all denied → should DENY (Require wins) */
    setup_htaccess("/var/www/.htaccess",
        "Order Deny,Allow\n"
        "Allow from all\n"
        "Require all denied\n");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/index.html");
    session_.set_client_ip("10.0.0.1");

    int rc = call_req_hook();
    EXPECT_EQ(session_.get_status_code(), 403);
}

/* ================================================================== */
/*  <Files> container ACL                                              */
/* ================================================================== */

TEST_F(ContainerSemanticsTest, FilesRequireAllDenied) {
    setup_htaccess("/var/www/.htaccess",
        "<Files secret.txt>\n"
        "Require all denied\n"
        "</Files>\n");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/secret.txt");
    session_.set_client_ip("10.0.0.1");

    int rc = call_req_hook();
    EXPECT_EQ(session_.get_status_code(), 403);
}

TEST_F(ContainerSemanticsTest, FilesNonMatchingAllowed) {
    setup_htaccess("/var/www/.htaccess",
        "<Files secret.txt>\n"
        "Require all denied\n"
        "</Files>\n");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/public.html");
    session_.set_client_ip("10.0.0.1");

    int rc = call_req_hook();
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_NE(session_.get_status_code(), 403);
}

/* ================================================================== */
/*  <FilesMatch> container ACL                                         */
/* ================================================================== */

TEST_F(ContainerSemanticsTest, FilesMatchDenyEnvFiles) {
    setup_htaccess("/var/www/.htaccess",
        "<FilesMatch \"\\.(env|log)$\">\n"
        "Order allow,deny\n"
        "Deny from all\n"
        "</FilesMatch>\n");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/.env");
    session_.set_client_ip("10.0.0.1");

    int rc = call_req_hook();
    EXPECT_EQ(session_.get_status_code(), 403);
}

/* ================================================================== */
/*  <Limit> method restriction                                         */
/* ================================================================== */

TEST_F(ContainerSemanticsTest, LimitPostDenied) {
    setup_htaccess("/var/www/.htaccess",
        "<Limit POST>\n"
        "Require all denied\n"
        "</Limit>\n");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/index.html");
    session_.set_client_ip("10.0.0.1");
    session_.set_method("POST");

    int rc = call_req_hook();
    EXPECT_EQ(session_.get_status_code(), 403);
}

TEST_F(ContainerSemanticsTest, LimitPostAllowsGet) {
    setup_htaccess("/var/www/.htaccess",
        "<Limit POST>\n"
        "Require all denied\n"
        "</Limit>\n");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/index.html");
    session_.set_client_ip("10.0.0.1");
    session_.set_method("GET");

    int rc = call_req_hook();
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_NE(session_.get_status_code(), 403);
}

/* ================================================================== */
/*  Directory inheritance — child overrides parent                     */
/* ================================================================== */

TEST_F(ContainerSemanticsTest, ChildHeaderOverridesParent) {
    /* Parent sets X-Level=parent, child sets X-Level=child → child wins */
    setup_htaccess("/var/www/.htaccess",
        "Header set X-Level parent");
    setup_htaccess("/var/www/sub/.htaccess",
        "Header set X-Level child");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/sub/page.html");
    session_.set_client_ip("10.0.0.1");

    call_req_hook();
    call_resp_hook();

    EXPECT_EQ(session_.get_response_header("X-Level"), "child");
}

TEST_F(ContainerSemanticsTest, ChildExpiresDefaultOverridesParent) {
    /* Parent ExpiresDefault 3600, child 7200 → child's 7200 wins */
    setup_htaccess("/var/www/.htaccess",
        "ExpiresActive On\nExpiresDefault \"access plus 3600 seconds\"");
    setup_htaccess("/var/www/sub/.htaccess",
        "ExpiresActive On\nExpiresDefault \"access plus 7200 seconds\"");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/sub/page.html");
    session_.set_client_ip("10.0.0.1");
    session_.set_status_code(200);

    call_req_hook();
    /* Set Content-Type for Expires matching */
    session_.add_response_header("Content-Type", "text/html");
    call_resp_hook();

    /* Check Cache-Control has 7200, not 3600 */
    auto cc = session_.get_response_header("Cache-Control");
    EXPECT_NE(cc.find("7200"), std::string::npos);
}

/* ================================================================== */
/*  P1: Extended container/inheritance tests                           */
/* ================================================================== */

/* --- IfModule flattening (all blocks expanded) --- */

TEST_F(ContainerSemanticsTest, IfModulePositiveExpanded) {
    setup_htaccess("/var/www/.htaccess",
        "<IfModule mod_headers.c>\n"
        "Header set X-IfMod positive\n"
        "</IfModule>\n");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/page.html");
    session_.set_client_ip("10.0.0.1");

    call_req_hook();
    call_resp_hook();

    EXPECT_EQ(session_.get_response_header("X-IfMod"), "positive");
}

TEST_F(ContainerSemanticsTest, IfModuleNegatedAlsoExpanded) {
    /* Negated IfModule should ALSO expand (we can't verify module existence) */
    setup_htaccess("/var/www/.htaccess",
        "<IfModule !mod_nonexistent.c>\n"
        "Header set X-Negated yes\n"
        "</IfModule>\n");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/page.html");
    session_.set_client_ip("10.0.0.1");

    call_req_hook();
    call_resp_hook();

    EXPECT_EQ(session_.get_response_header("X-Negated"), "yes");
}

/* --- Nested RequireAll inside Files --- */

TEST_F(ContainerSemanticsTest, FilesWithNestedRequireAll) {
    setup_htaccess("/var/www/.htaccess",
        "<Files secret.txt>\n"
        "<RequireAll>\n"
        "Require all denied\n"
        "</RequireAll>\n"
        "</Files>\n");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/secret.txt");
    session_.set_client_ip("10.0.0.1");

    int rc = call_req_hook();
    EXPECT_EQ(session_.get_status_code(), 403);
}

/* --- LimitExcept (inverse of Limit) --- */

TEST_F(ContainerSemanticsTest, LimitExceptGetDenied) {
    /* <LimitExcept POST> Require all denied → GET denied, POST allowed */
    setup_htaccess("/var/www/.htaccess",
        "<LimitExcept POST>\n"
        "Require all denied\n"
        "</LimitExcept>\n");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/index.html");
    session_.set_client_ip("10.0.0.1");
    session_.set_method("GET");

    call_req_hook();
    EXPECT_EQ(session_.get_status_code(), 403);
}

TEST_F(ContainerSemanticsTest, LimitExceptPostAllowed) {
    setup_htaccess("/var/www/.htaccess",
        "<LimitExcept POST>\n"
        "Require all denied\n"
        "</LimitExcept>\n");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/index.html");
    session_.set_client_ip("10.0.0.1");
    session_.set_method("POST");

    int rc = call_req_hook();
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_NE(session_.get_status_code(), 403);
}

/* --- Legacy ACL only (no Require) → still works --- */

TEST_F(ContainerSemanticsTest, LegacyACLAloneWorks) {
    setup_htaccess("/var/www/.htaccess",
        "Order Allow,Deny\n"
        "Deny from all\n");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/index.html");
    session_.set_client_ip("10.0.0.1");

    call_req_hook();
    EXPECT_EQ(session_.get_status_code(), 403);
}

/* --- Multiple Header directives across parent/child --- */

TEST_F(ContainerSemanticsTest, ChildAddsNewHeaderKeepsParent) {
    /* Parent sets X-Parent, child sets X-Child → both present */
    setup_htaccess("/var/www/.htaccess",
        "Header set X-Parent yes");
    setup_htaccess("/var/www/sub/.htaccess",
        "Header set X-Child yes");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/sub/page.html");
    session_.set_client_ip("10.0.0.1");

    call_req_hook();
    call_resp_hook();

    EXPECT_EQ(session_.get_response_header("X-Parent"), "yes");
    EXPECT_EQ(session_.get_response_header("X-Child"), "yes");
}

/* --- DirectoryIndex singleton override --- */

TEST_F(ContainerSemanticsTest, ChildDirectoryIndexOverridesParent) {
    setup_htaccess("/var/www/.htaccess",
        "DirectoryIndex index.html");
    setup_htaccess("/var/www/sub/.htaccess",
        "DirectoryIndex index.php");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/sub/");
    session_.set_client_ip("10.0.0.1");
    session_.add_existing_file("/var/www/sub/index.php");

    call_req_hook();

    /* Child's index.php should be found, not parent's index.html */
    EXPECT_EQ(session_.get_internal_uri(), "/sub/index.php");
}

/* ================================================================== */
/*  行为一致性专项: 扩展继承和容器测试                                  */
/* ================================================================== */

/* --- SetEnvIfNoCase override --- */

TEST_F(ContainerSemanticsTest, SetEnvIfNoCaseInChild) {
    setup_htaccess("/var/www/.htaccess",
        "SetEnvIfNoCase User-Agent bot IS_BOT=1");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/page.html");
    session_.set_client_ip("10.0.0.1");
    session_.add_request_header("User-Agent", "Googlebot/2.1");

    call_req_hook();

    EXPECT_TRUE(session_.has_env_var("IS_BOT"));
    EXPECT_EQ(session_.get_env_var("IS_BOT"), "1");
}

/* --- IfModule in child directory --- */

TEST_F(ContainerSemanticsTest, IfModuleInChildDir) {
    setup_htaccess("/var/www/.htaccess",
        "Header set X-Root root");
    setup_htaccess("/var/www/sub/.htaccess",
        "<IfModule mod_headers.c>\n"
        "Header set X-Sub child\n"
        "</IfModule>\n");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/sub/page.html");
    session_.set_client_ip("10.0.0.1");

    call_req_hook();
    call_resp_hook();

    EXPECT_EQ(session_.get_response_header("X-Root"), "root");
    EXPECT_EQ(session_.get_response_header("X-Sub"), "child");
}

/* --- DirectoryIndex child file doesn't exist → no internal redirect --- */

TEST_F(ContainerSemanticsTest, DirectoryIndexChildFileNotExist) {
    setup_htaccess("/var/www/.htaccess",
        "DirectoryIndex default.html");
    setup_htaccess("/var/www/sub/.htaccess",
        "DirectoryIndex custom.html");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/sub/");
    session_.set_client_ip("10.0.0.1");
    /* Neither file exists */

    call_req_hook();

    /* No internal redirect should be set */
    EXPECT_TRUE(session_.get_internal_uri().empty());
}

/* --- php_value in child directory --- */

TEST_F(ContainerSemanticsTest, PhpValueInChildDir) {
    setup_htaccess("/var/www/.htaccess",
        "php_value upload_max_filesize 8M");
    setup_htaccess("/var/www/sub/.htaccess",
        "php_value upload_max_filesize 128M");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/sub/upload.php");
    session_.set_client_ip("10.0.0.1");

    call_req_hook();

    /* Child's 128M should override parent's 8M */
    auto &records = session_.get_php_ini_records();
    ASSERT_FALSE(records.empty());
    /* Last record should be the child's value */
    bool found_128 = false;
    for (const auto &r : records) {
        if (r.name == "upload_max_filesize" && r.value == "128M")
            found_128 = true;
    }
    EXPECT_TRUE(found_128);
}

/* --- Redirect in child directory --- */

TEST_F(ContainerSemanticsTest, RedirectInChildDir) {
    setup_htaccess("/var/www/sub/.htaccess",
        "Redirect 301 /sub/old /sub/new");

    session_.set_doc_root("/var/www");
    session_.set_request_uri("/sub/old");
    session_.set_client_ip("10.0.0.1");

    call_req_hook();

    EXPECT_EQ(session_.get_status_code(), 301);
    EXPECT_TRUE(session_.has_response_header("Location"));
}
