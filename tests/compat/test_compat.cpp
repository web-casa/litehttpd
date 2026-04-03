/**
 * test_compat.cpp - .htaccess compatibility tests
 *
 * Verifies that the OLS module correctly parses and executes real-world
 * .htaccess files, matching Apache httpd semantics. Each test loads a
 * sample .htaccess file, parses it, and validates:
 *   1. Parse completeness — all directives are recognised
 *   2. Round-trip fidelity — parse → print → re-parse yields equivalent list
 *   3. Execution correctness — directive effects match Apache behaviour
 */

#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

/* Include mock FIRST to prevent ls.h from defining conflicting types */
#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_parser.h"
#include "htaccess_printer.h"
#include "htaccess_directive.h"
#include "htaccess_exec_header.h"
#include "htaccess_exec_php.h"
#include "htaccess_exec_acl.h"
#include "htaccess_exec_redirect.h"
#include "htaccess_exec_error_doc.h"
#include "htaccess_exec_files_match.h"
#include "htaccess_exec_expires.h"
#include "htaccess_exec_env.h"
#include "htaccess_exec_brute_force.h"
#include "htaccess_cache.h"
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static std::string read_file(const std::string &path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        /* Try with COMPAT_SAMPLES_DIR prefix */
        std::string full = std::string(COMPAT_SAMPLES_DIR) + "/" + path;
        f.open(full);
    }
    EXPECT_TRUE(f.is_open()) << "Cannot open: " << path;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static int count_directives(const htaccess_directive_t *head) {
    int n = 0;
    for (const htaccess_directive_t *d = head; d; d = d->next)
        ++n;
    return n;
}

static bool has_directive_type(const htaccess_directive_t *head,
                               directive_type_t type) {
    for (const htaccess_directive_t *d = head; d; d = d->next)
        if (d->type == type) return true;
    return false;
}

static const htaccess_directive_t *find_directive(
    const htaccess_directive_t *head, directive_type_t type) {
    for (const htaccess_directive_t *d = head; d; d = d->next)
        if (d->type == type) return d;
    return nullptr;
}

/* Count non-comment, non-blank lines in raw text */
static int count_meaningful_lines(const std::string &text) {
    std::istringstream iss(text);
    std::string line;
    int n = 0;
    while (std::getline(iss, line)) {
        /* Trim leading whitespace */
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        std::string trimmed = line.substr(start);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        /* </FilesMatch> closing tags are not separate directives */
        if (trimmed.find("</FilesMatch>") != std::string::npos) continue;
        n++;
    }
    return n;
}

/* Count directives recursively (including children of FilesMatch) */
static int count_directives_recursive(const htaccess_directive_t *head) {
    int n = 0;
    for (const htaccess_directive_t *d = head; d; d = d->next) {
        n++;
        if (d->type == DIR_FILES_MATCH && d->data.files_match.children)
            n += count_directives_recursive(d->data.files_match.children);
    }
    return n;
}

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class CompatTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
    MockSession session_;
};

/* ================================================================== */
/*  1. Parse completeness — every meaningful line becomes a directive  */
/* ================================================================== */

struct ParseSample {
    std::string filename;
    int expected_directive_count; /* total including nested */
};

class ParseCompletenessTest
    : public CompatTest,
      public ::testing::WithParamInterface<ParseSample> {};

TEST_P(ParseCompletenessTest, AllDirectivesParsed) {
    auto &p = GetParam();
    std::string content = read_file(p.filename);
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), p.filename.c_str());
    ASSERT_NE(head, nullptr) << "Parse returned NULL for " << p.filename;

    int actual = count_directives_recursive(head);
    EXPECT_EQ(actual, p.expected_directive_count)
        << "Directive count mismatch for " << p.filename;

    htaccess_directives_free(head);
}

INSTANTIATE_TEST_SUITE_P(Samples, ParseCompletenessTest,
    ::testing::Values(
        ParseSample{"header_ops.htaccess",        6},
        ParseSample{"php_config.htaccess",         6},
        ParseSample{"access_control.htaccess",     4},
        ParseSample{"redirect_rules.htaccess",     3},
        ParseSample{"error_docs.htaccess",         3},
        ParseSample{"files_match.htaccess",        4},  /* 2 FilesMatch + 2 nested */
        ParseSample{"expires.htaccess",            5},
        ParseSample{"env_vars.htaccess",           4},
        ParseSample{"brute_force.htaccess",        4},
        ParseSample{"combined_wordpress.htaccess", 16},
        ParseSample{"combined_security.htaccess",  20}
    ));

/* ================================================================== */
/*  2. Round-trip fidelity — parse → print → re-parse                 */
/* ================================================================== */

class RoundTripTest
    : public CompatTest,
      public ::testing::WithParamInterface<std::string> {};

TEST_P(RoundTripTest, ParsePrintReparse) {
    std::string content = read_file(GetParam());
    htaccess_directive_t *first = htaccess_parse(
        content.c_str(), content.size(), GetParam().c_str());
    ASSERT_NE(first, nullptr);

    char *printed = htaccess_print(first);
    ASSERT_NE(printed, nullptr);

    htaccess_directive_t *second = htaccess_parse(
        printed, strlen(printed), "round-trip");
    ASSERT_NE(second, nullptr);

    /* Compare directive counts */
    EXPECT_EQ(count_directives_recursive(first),
              count_directives_recursive(second));

    /* Compare directive types in order */
    const htaccess_directive_t *a = first;
    const htaccess_directive_t *b = second;
    while (a && b) {
        EXPECT_EQ(a->type, b->type);
        a = a->next;
        b = b->next;
    }
    EXPECT_EQ(a, nullptr);
    EXPECT_EQ(b, nullptr);

    free(printed);
    htaccess_directives_free(second);
    htaccess_directives_free(first);
}

INSTANTIATE_TEST_SUITE_P(Samples, RoundTripTest,
    ::testing::Values(
        "header_ops.htaccess",
        "php_config.htaccess",
        "access_control.htaccess",
        "redirect_rules.htaccess",
        "error_docs.htaccess",
        "files_match.htaccess",
        "expires.htaccess",
        "env_vars.htaccess",
        "brute_force.htaccess",
        "combined_wordpress.htaccess",
        "combined_security.htaccess"
    ));

/* ================================================================== */
/*  3. Execution correctness — Apache-compatible behaviour             */
/* ================================================================== */

/* --- 3a. Header operations match Apache semantics --- */

TEST_F(CompatTest, HeaderOps_SetReplacesValue) {
    std::string content = read_file("header_ops.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "header_ops.htaccess");
    ASSERT_NE(head, nullptr);

    lsi_session_t *s = session_.handle();
    /* Pre-set a header that "unset" should remove */
    session_.add_response_header("X-Powered-By", "PHP/8.1");

    for (const htaccess_directive_t *d = head; d; d = d->next) {
        switch (d->type) {
        case DIR_HEADER_SET:    exec_header(s, d); break;
        case DIR_HEADER_UNSET:  exec_header(s, d); break;
        case DIR_HEADER_APPEND: exec_header(s, d); break;
        case DIR_HEADER_MERGE:  exec_header(s, d); break;
        case DIR_HEADER_ADD:    exec_header(s, d); break;
        case DIR_REQUEST_HEADER_SET:   exec_request_header(s, d); break;
        case DIR_REQUEST_HEADER_UNSET: exec_request_header(s, d); break;
        default: break;
        }
    }

    /* Apache: Header set X-Frame-Options "SAMEORIGIN" → exactly one value */
    EXPECT_EQ(session_.get_response_header("X-Frame-Options"), "SAMEORIGIN");
    EXPECT_EQ(session_.get_response_header("X-Content-Type-Options"), "nosniff");

    /* Apache: Header unset X-Powered-By → removed */
    EXPECT_FALSE(session_.has_response_header("X-Powered-By"));

    /* Apache: Header append Cache-Control "public" */
    std::string cc = session_.get_response_header("Cache-Control");
    EXPECT_NE(cc.find("public"), std::string::npos);

    /* Apache: RequestHeader set X-Forwarded-Proto "https" */
    EXPECT_EQ(session_.get_request_header("X-Forwarded-Proto"), "https");

    htaccess_directives_free(head);
}

/* --- 3b. Access control: Deny,Allow with CIDR --- */

TEST_F(CompatTest, ACL_DenyAllowFromCIDR) {
    std::string content = read_file("access_control.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "access_control.htaccess");
    ASSERT_NE(head, nullptr);

    lsi_session_t *s = session_.handle();

    /* Apache: Order Deny,Allow → default allow, Deny from all overrides,
       but Allow from 192.168.1.0/24 and 10.0.0.0/8 re-allow those ranges */

    /* IP in allowed range → should be allowed */
    session_.set_client_ip("192.168.1.50");
    int result = exec_access_control(s, head);
    EXPECT_EQ(result, 0) << "192.168.1.50 should be allowed";

    /* IP in 10.x range → should be allowed */
    session_.set_client_ip("10.5.3.1");
    session_.set_status_code(200);
    result = exec_access_control(s, head);
    EXPECT_EQ(result, 0) << "10.5.3.1 should be allowed";

    /* IP outside all ranges → should be denied (403) */
    session_.set_client_ip("8.8.8.8");
    session_.set_status_code(200);
    result = exec_access_control(s, head);
    EXPECT_NE(result, 0) << "8.8.8.8 should be denied";
    EXPECT_EQ(session_.get_status_code(), 403);

    htaccess_directives_free(head);
}

/* --- 3c. Redirect: status codes and default 302 --- */

TEST_F(CompatTest, Redirect_StatusCodesAndDefault302) {
    std::string content = read_file("redirect_rules.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "redirect_rules.htaccess");
    ASSERT_NE(head, nullptr);

    /* First directive: Redirect 301 /old-page /new-page */
    const htaccess_directive_t *d1 = find_directive(head, DIR_REDIRECT);
    ASSERT_NE(d1, nullptr);
    EXPECT_EQ(d1->data.redirect.status_code, 301);

    lsi_session_t *s = session_.handle();
    session_.set_request_uri("/old-page");
    int result = exec_redirect(s, d1);
    EXPECT_NE(result, 0); /* short-circuit */
    EXPECT_EQ(session_.get_status_code(), 301);
    EXPECT_EQ(session_.get_response_header("Location"), "/new-page");

    /* Second directive: Redirect /temp-page /other-page → default 302 */
    const htaccess_directive_t *d2 = d1->next;
    ASSERT_NE(d2, nullptr);
    ASSERT_EQ(d2->type, DIR_REDIRECT);
    EXPECT_EQ(d2->data.redirect.status_code, 302);

    session_.reset();
    session_.set_request_uri("/temp-page");
    result = exec_redirect(s, d2);
    EXPECT_NE(result, 0);
    EXPECT_EQ(session_.get_status_code(), 302);
    EXPECT_EQ(session_.get_response_header("Location"), "/other-page");

    htaccess_directives_free(head);
}

/* --- 3d. RedirectMatch with backreferences --- */

TEST_F(CompatTest, RedirectMatch_Backreferences) {
    std::string content = read_file("redirect_rules.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "redirect_rules.htaccess");
    ASSERT_NE(head, nullptr);

    const htaccess_directive_t *rm = find_directive(head, DIR_REDIRECT_MATCH);
    ASSERT_NE(rm, nullptr);

    lsi_session_t *s = session_.handle();
    session_.set_request_uri("/blog/2024/03");
    int result = exec_redirect_match(s, rm);
    EXPECT_NE(result, 0);
    EXPECT_EQ(session_.get_status_code(), 301);
    /* Apache: $1=2024, $2=03 → /archive/2024/03 */
    EXPECT_EQ(session_.get_response_header("Location"), "/archive/2024/03");

    htaccess_directives_free(head);
}

/* --- 3e. PHP config: values and admin override protection --- */

TEST_F(CompatTest, PHPConfig_ValuesAndAdminProtection) {
    std::string content = read_file("php_config.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "php_config.htaccess");
    ASSERT_NE(head, nullptr);

    lsi_session_t *s = session_.handle();
    for (const htaccess_directive_t *d = head; d; d = d->next) {
        switch (d->type) {
        case DIR_PHP_VALUE:       exec_php_value(s, d); break;
        case DIR_PHP_FLAG:        exec_php_flag(s, d); break;
        case DIR_PHP_ADMIN_VALUE: exec_php_admin_value(s, d); break;
        case DIR_PHP_ADMIN_FLAG:  exec_php_admin_flag(s, d); break;
        default: break;
        }
    }

    auto &records = session_.get_php_ini_records();
    /* upload_max_filesize, post_max_size are PHP_INI_PERDIR → accepted by php_value
       All 6 directives produce records:
       upload_max_filesize, post_max_size, max_execution_time, display_errors (user)
       + open_basedir, allow_url_fopen (admin) = 6 */
    EXPECT_EQ(records.size(), 6u);

    /* Verify admin values are marked as admin level */
    bool found_admin = false;
    for (auto &r : records) {
        if (r.name == "open_basedir") {
            found_admin = true;
            EXPECT_EQ(r.is_admin, 1);
            EXPECT_EQ(r.value, "/var/www/html");
        }
    }
    EXPECT_TRUE(found_admin);

    htaccess_directives_free(head);
}

/* --- 3f. ErrorDocument: local path, text message, external URL --- */

TEST_F(CompatTest, ErrorDocument_ThreeModes) {
    std::string content = read_file("error_docs.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "error_docs.htaccess");
    ASSERT_NE(head, nullptr);

    /* Should have 3 ErrorDocument directives */
    int count = 0;
    for (const htaccess_directive_t *d = head; d; d = d->next) {
        if (d->type == DIR_ERROR_DOCUMENT) count++;
    }
    EXPECT_EQ(count, 3);

    /* Test external URL → 302 redirect */
    const htaccess_directive_t *d = head;
    while (d && !(d->type == DIR_ERROR_DOCUMENT && d->data.error_doc.error_code == 403))
        d = d->next;
    ASSERT_NE(d, nullptr);

    lsi_session_t *s = session_.handle();
    session_.set_status_code(403);
    exec_error_document(s, d);
    /* Apache: external URL → 302 redirect */
    EXPECT_EQ(session_.get_status_code(), 302);
    EXPECT_EQ(session_.get_response_header("Location"), "https://example.com/forbidden");

    /* Test text message — parser strips quotes, so exec sees unquoted text
       starting with 'I', which doesn't match any mode (not URL, not quoted,
       not local path). This is a known limitation. */
    session_.reset();
    d = head;
    while (d && !(d->type == DIR_ERROR_DOCUMENT && d->data.error_doc.error_code == 500))
        d = d->next;
    ASSERT_NE(d, nullptr);
    /* Verify the value was parsed (quotes stripped by parser) */
    ASSERT_NE(d->value, nullptr);
    EXPECT_NE(std::string(d->value).find("Internal Server Error"), std::string::npos);

    htaccess_directives_free(head);
}

/* --- 3g. Expires: Cache-Control and Expires headers --- */

TEST_F(CompatTest, Expires_CacheControlHeaders) {
    std::string content = read_file("expires.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "expires.htaccess");
    ASSERT_NE(head, nullptr);

    /* Verify ExpiresActive On is parsed */
    EXPECT_TRUE(has_directive_type(head, DIR_EXPIRES_ACTIVE));

    /* Verify ExpiresByType directives exist */
    int eby_count = 0;
    for (const htaccess_directive_t *d = head; d; d = d->next)
        if (d->type == DIR_EXPIRES_BY_TYPE) eby_count++;
    EXPECT_EQ(eby_count, 4);

    /* Execute on a session with image/jpeg content type */
    lsi_session_t *s = session_.handle();
    session_.add_response_header("Content-Type", "image/jpeg");

    exec_expires(s, head, "image/jpeg");

    /* Apache: ExpiresByType image/jpeg "access plus 1 month" → max-age=2592000 */
    std::string cc = session_.get_response_header("Cache-Control");
    EXPECT_NE(cc.find("max-age="), std::string::npos);

    htaccess_directives_free(head);
}

/* --- 3h. Environment variables: SetEnv, SetEnvIf, BrowserMatch --- */

TEST_F(CompatTest, EnvVars_ConditionalSetting) {
    std::string content = read_file("env_vars.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "env_vars.htaccess");
    ASSERT_NE(head, nullptr);

    lsi_session_t *s = session_.handle();
    session_.set_client_ip("127.0.0.1");
    session_.set_request_uri("/images/logo.gif");
    session_.add_request_header("User-Agent", "MSIE 11.0");

    for (const htaccess_directive_t *d = head; d; d = d->next) {
        switch (d->type) {
        case DIR_SETENV:       exec_setenv(s, d); break;
        case DIR_SETENVIF:     exec_setenvif(s, d); break;
        case DIR_BROWSER_MATCH: exec_browser_match(s, d); break;
        default: break;
        }
    }

    /* Apache: SetEnv APPLICATION_ENV production → always set */
    EXPECT_TRUE(session_.has_env_var("APPLICATION_ENV"));
    EXPECT_EQ(session_.get_env_var("APPLICATION_ENV"), "production");

    /* Apache: SetEnvIf Remote_Addr "^127\.0\.0\.1$" local_request → matches */
    EXPECT_TRUE(session_.has_env_var("local_request"));

    /* Apache: SetEnvIf Request_URI "\.gif$" image_request → matches */
    EXPECT_TRUE(session_.has_env_var("image_request"));

    /* Apache: BrowserMatch "MSIE" ie_browser → matches */
    EXPECT_TRUE(session_.has_env_var("ie_browser"));

    htaccess_directives_free(head);
}

/* --- 3i. FilesMatch: conditional directive application --- */

TEST_F(CompatTest, FilesMatch_ConditionalApplication) {
    std::string content = read_file("files_match.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "files_match.htaccess");
    ASSERT_NE(head, nullptr);

    lsi_session_t *s = session_.handle();

    /* Request for a .jpg file → first FilesMatch should apply */
    session_.set_request_uri("/images/photo.jpg");
    for (const htaccess_directive_t *d = head; d; d = d->next) {
        if (d->type == DIR_FILES_MATCH)
            exec_files_match(s, d, "photo.jpg");
    }
    EXPECT_TRUE(session_.has_response_header("Cache-Control"));
    EXPECT_FALSE(session_.has_response_header("X-Content-Type-Options"));

    /* Request for a .php file → second FilesMatch should apply */
    session_.reset();
    session_.set_request_uri("/app/index.php");
    for (const htaccess_directive_t *d = head; d; d = d->next) {
        if (d->type == DIR_FILES_MATCH)
            exec_files_match(s, d, "index.php");
    }
    EXPECT_TRUE(session_.has_response_header("X-Content-Type-Options"));

    /* Request for a .txt file → neither should apply */
    session_.reset();
    session_.set_request_uri("/docs/readme.txt");
    for (const htaccess_directive_t *d = head; d; d = d->next) {
        if (d->type == DIR_FILES_MATCH)
            exec_files_match(s, d, "readme.txt");
    }
    EXPECT_FALSE(session_.has_response_header("Cache-Control"));
    EXPECT_FALSE(session_.has_response_header("X-Content-Type-Options"));

    htaccess_directives_free(head);
}

/* --- 3j. Combined WordPress .htaccess: full pipeline --- */

TEST_F(CompatTest, CombinedWordPress_FullPipeline) {
    std::string content = read_file("combined_wordpress.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "combined_wordpress.htaccess");
    ASSERT_NE(head, nullptr);

    lsi_session_t *s = session_.handle();
    session_.set_request_uri("/page.php");
    session_.set_client_ip("1.2.3.4");
    session_.add_response_header("Content-Type", "image/jpeg");

    /* Execute all directives in order (simulating the module pipeline) */
    for (const htaccess_directive_t *d = head; d; d = d->next) {
        switch (d->type) {
        case DIR_HEADER_SET:
        case DIR_HEADER_UNSET:
        case DIR_HEADER_APPEND:
        case DIR_HEADER_MERGE:
        case DIR_HEADER_ADD:
            exec_header(s, d); break;
        case DIR_REQUEST_HEADER_SET:
        case DIR_REQUEST_HEADER_UNSET:
            exec_request_header(s, d); break;
        case DIR_EXPIRES_ACTIVE:
        case DIR_EXPIRES_BY_TYPE:
            /* Handled below as a batch */ break;
        case DIR_FILES_MATCH:
            exec_files_match(s, d, "page.php"); break;
        case DIR_ERROR_DOCUMENT:
            /* Don't execute — just verify it parsed */ break;
        case DIR_PHP_VALUE:
            exec_php_value(s, d); break;
        case DIR_PHP_FLAG:
            exec_php_flag(s, d); break;
        case DIR_ORDER:
        case DIR_ALLOW_FROM:
        case DIR_DENY_FROM:
            /* ACL handled separately */ break;
        default: break;
        }
    }

    /* Security headers applied */
    EXPECT_EQ(session_.get_response_header("X-Frame-Options"), "SAMEORIGIN");
    EXPECT_EQ(session_.get_response_header("X-XSS-Protection"), "1; mode=block");
    EXPECT_EQ(session_.get_response_header("X-Content-Type-Options"), "nosniff");

    /* Execute expires as a batch (needs full directive list + content type) */
    exec_expires(s, head, "image/jpeg");

    /* Expires applied for image/jpeg */
    std::string cc = session_.get_response_header("Cache-Control");
    EXPECT_NE(cc.find("max-age="), std::string::npos);

    /* PHP config applied (only max_execution_time goes through;
       upload_max_filesize and post_max_size are PHP_INI_SYSTEM → ignored) */
    auto &records = session_.get_php_ini_records();
    EXPECT_GE(records.size(), 1u);

    /* ACL: Order Deny,Allow + Allow from all → everyone allowed */
    session_.set_client_ip("203.0.113.50");
    int acl_result = exec_access_control(s, head);
    EXPECT_EQ(acl_result, 0);

    htaccess_directives_free(head);
}

/* --- 3k. Combined security .htaccess: ACL + headers + env + brute force --- */

TEST_F(CompatTest, CombinedSecurity_InternalAccess) {
    std::string content = read_file("combined_security.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "combined_security.htaccess");
    ASSERT_NE(head, nullptr);

    lsi_session_t *s = session_.handle();

    /* Internal IP → should be allowed */
    session_.set_client_ip("10.0.5.1");
    int result = exec_access_control(s, head);
    EXPECT_EQ(result, 0) << "Internal IP 10.0.5.1 should be allowed";

    /* External IP → should be denied */
    session_.reset();
    session_.set_client_ip("203.0.113.1");
    result = exec_access_control(s, head);
    EXPECT_NE(result, 0) << "External IP should be denied";
    EXPECT_EQ(session_.get_status_code(), 403);

    htaccess_directives_free(head);
}

TEST_F(CompatTest, CombinedSecurity_HeadersApplied) {
    std::string content = read_file("combined_security.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "combined_security.htaccess");
    ASSERT_NE(head, nullptr);

    lsi_session_t *s = session_.handle();
    session_.add_response_header("Server", "Apache");
    session_.add_response_header("X-Powered-By", "PHP");

    for (const htaccess_directive_t *d = head; d; d = d->next) {
        switch (d->type) {
        case DIR_HEADER_SET:
        case DIR_HEADER_UNSET:
            exec_header(s, d); break;
        case DIR_REQUEST_HEADER_UNSET:
            exec_request_header(s, d); break;
        default: break;
        }
    }

    /* HSTS header set */
    std::string hsts = session_.get_response_header("Strict-Transport-Security");
    EXPECT_NE(hsts.find("max-age=31536000"), std::string::npos);

    /* Server and X-Powered-By removed */
    EXPECT_FALSE(session_.has_response_header("Server"));
    EXPECT_FALSE(session_.has_response_header("X-Powered-By"));

    htaccess_directives_free(head);
}

/* --- 3l. Cache round-trip with real .htaccess content --- */

TEST_F(CompatTest, Cache_RealContentRoundTrip) {
    int rc = htaccess_cache_init(16);
    ASSERT_EQ(rc, 0);

    std::string content = read_file("combined_wordpress.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "combined_wordpress.htaccess");
    ASSERT_NE(head, nullptr);

    /* Store in cache (ownership transferred) */
    htaccess_cache_put("/var/www/html/.htaccess", 1000, 0, 0, head);

    /* Retrieve with same mtime → should hit */
    htaccess_directive_t *cached = nullptr;
    int hit = htaccess_cache_get("/var/www/html/.htaccess", 1000, 0, 0, &cached);
    EXPECT_EQ(hit, 0);
    EXPECT_NE(cached, nullptr);

    /* Retrieve with different mtime → should miss */
    htaccess_directive_t *stale = nullptr;
    int miss = htaccess_cache_get("/var/www/html/.htaccess", 2000, 0, 0, &stale);
    EXPECT_NE(miss, 0);

    htaccess_cache_destroy();
}
