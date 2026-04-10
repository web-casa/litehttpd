/**
 * test_compat_v2.cpp - v2 compatibility tests
 *
 * Verifies v2 directive parsing, round-trip, and execution using
 * real-world .htaccess sample files.
 */
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_parser.h"
#include "htaccess_printer.h"
#include "htaccess_directive.h"
#include "htaccess_exec_header.h"
#include "htaccess_exec_options.h"
#include "htaccess_exec_require.h"
#include "htaccess_exec_handler.h"
#include "htaccess_exec_dirindex.h"
#include "htaccess_exec_forcetype.h"
#include "htaccess_exec_encoding.h"
#include "htaccess_exec_expires.h"
}

static std::string read_file(const std::string &path) {
    std::string full = std::string(COMPAT_SAMPLES_DIR) + "/" + path;
    std::ifstream f(full);
    EXPECT_TRUE(f.is_open()) << "Cannot open: " << full;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static int count_directives_recursive(const htaccess_directive_t *head) {
    int n = 0;
    for (const htaccess_directive_t *d = head; d; d = d->next) {
        n++;
        switch (d->type) {
        case DIR_FILES_MATCH:
            n += count_directives_recursive(d->data.files_match.children);
            break;
        case DIR_IFMODULE:
            n += count_directives_recursive(d->data.ifmodule.children);
            break;
        case DIR_FILES:
            n += count_directives_recursive(d->data.files.children);
            break;
        case DIR_REQUIRE_ANY_OPEN:
        case DIR_REQUIRE_ALL_OPEN:
            n += count_directives_recursive(d->data.require_container.children);
            break;
        case DIR_LIMIT:
        case DIR_LIMIT_EXCEPT:
            n += count_directives_recursive(d->data.limit.children);
            break;
        default:
            break;
        }
    }
    return n;
}

static bool has_type(const htaccess_directive_t *head, directive_type_t type) {
    for (const htaccess_directive_t *d = head; d; d = d->next)
        if (d->type == type) return true;
    return false;
}

static bool directives_equivalent(const htaccess_directive_t *a,
                                  const htaccess_directive_t *b);

static bool compare_children(const htaccess_directive_t *a,
                             const htaccess_directive_t *b) {
    return directives_equivalent(a, b);
}

static bool directives_equivalent(const htaccess_directive_t *a,
                                  const htaccess_directive_t *b) {
    while (a && b) {
        if (a->type != b->type) return false;
        if ((a->name == nullptr) != (b->name == nullptr)) return false;
        if (a->name && b->name && strcmp(a->name, b->name) != 0) return false;
        if ((a->value == nullptr) != (b->value == nullptr)) return false;
        if (a->value && b->value && strcmp(a->value, b->value) != 0) return false;

        switch (a->type) {
        case DIR_IFMODULE:
            if (!compare_children(a->data.ifmodule.children,
                                  b->data.ifmodule.children))
                return false;
            break;
        case DIR_FILES:
            if (!compare_children(a->data.files.children,
                                  b->data.files.children))
                return false;
            break;
        case DIR_REQUIRE_ANY_OPEN:
        case DIR_REQUIRE_ALL_OPEN:
            if (!compare_children(a->data.require_container.children,
                                  b->data.require_container.children))
                return false;
            break;
        case DIR_LIMIT:
        case DIR_LIMIT_EXCEPT:
            if ((a->data.limit.methods == nullptr) !=
                (b->data.limit.methods == nullptr))
                return false;
            if (a->data.limit.methods && b->data.limit.methods &&
                strcmp(a->data.limit.methods, b->data.limit.methods) != 0)
                return false;
            if (!compare_children(a->data.limit.children,
                                  b->data.limit.children))
                return false;
            break;
        case DIR_FILES_MATCH:
            if ((a->data.files_match.pattern == nullptr) !=
                (b->data.files_match.pattern == nullptr))
                return false;
            if (a->data.files_match.pattern && b->data.files_match.pattern &&
                strcmp(a->data.files_match.pattern,
                       b->data.files_match.pattern) != 0)
                return false;
            if (!compare_children(a->data.files_match.children,
                                  b->data.files_match.children))
                return false;
            break;
        case DIR_ORDER:
            if (a->data.acl.order != b->data.acl.order) return false;
            break;
        case DIR_EXPIRES_ACTIVE:
            if (a->data.expires.active != b->data.expires.active) return false;
            break;
        case DIR_EXPIRES_BY_TYPE:
        case DIR_EXPIRES_DEFAULT:
            if (a->data.expires.duration_sec != b->data.expires.duration_sec)
                return false;
            break;
        case DIR_BRUTE_FORCE_PROTECTION:
        case DIR_BRUTE_FORCE_X_FORWARDED_FOR:
            if (a->data.brute_force.enabled != b->data.brute_force.enabled)
                return false;
            break;
        case DIR_OPTIONS:
            if (a->data.options.indexes != b->data.options.indexes) return false;
            if (a->data.options.follow_symlinks != b->data.options.follow_symlinks)
                return false;
            if (a->data.options.multiviews != b->data.options.multiviews)
                return false;
            if (a->data.options.exec_cgi != b->data.options.exec_cgi) return false;
            break;
        default:
            break;
        }
        a = a->next;
        b = b->next;
    }
    return (a == nullptr && b == nullptr);
}

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class CompatV2Test : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
    MockSession session_;
};

/* ================================================================== */
/*  Parse completeness tests                                           */
/* ================================================================== */

struct V2ParseSample {
    std::string filename;
    int expected_count;
};

class V2ParseTest : public CompatV2Test,
                    public ::testing::WithParamInterface<V2ParseSample> {};

TEST_P(V2ParseTest, AllDirectivesParsed) {
    auto &p = GetParam();
    std::string content = read_file(p.filename);
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), p.filename.c_str());
    ASSERT_NE(head, nullptr);
    EXPECT_EQ(count_directives_recursive(head), p.expected_count)
        << "Directive count mismatch for " << p.filename;
    htaccess_directives_free(head);
}

INSTANTIATE_TEST_SUITE_P(V2Samples, V2ParseTest,
    ::testing::Values(
        V2ParseSample{"wordpress_ifmodule.htaccess", 5},
        V2ParseSample{"cpanel_auth_basic.htaccess", 4},
        V2ParseSample{"apache24_require.htaccess", 3},
        V2ParseSample{"security_headers.htaccess", 5},
        V2ParseSample{"laravel_options.htaccess", 4},
        V2ParseSample{"cyberpanel_full.htaccess", 19}
    ));

/* ================================================================== */
/*  Round-trip tests                                                   */
/* ================================================================== */

class V2RoundTripTest : public CompatV2Test,
                        public ::testing::WithParamInterface<std::string> {};

TEST_P(V2RoundTripTest, ParsePrintReparse) {
    std::string content = read_file(GetParam());
    htaccess_directive_t *p1 = htaccess_parse(
        content.c_str(), content.size(), GetParam().c_str());
    ASSERT_NE(p1, nullptr);

    char *printed = htaccess_print(p1);
    ASSERT_NE(printed, nullptr);

    htaccess_directive_t *p2 = htaccess_parse(
        printed, strlen(printed), "reparse");
    ASSERT_NE(p2, nullptr);
    EXPECT_TRUE(directives_equivalent(p1, p2));

    free(printed);
    htaccess_directives_free(p1);
    htaccess_directives_free(p2);
}

INSTANTIATE_TEST_SUITE_P(V2Samples, V2RoundTripTest,
    ::testing::Values(
        "wordpress_ifmodule.htaccess",
        "cpanel_auth_basic.htaccess",
        "apache24_require.htaccess",
        "security_headers.htaccess",
        "laravel_options.htaccess",
        "cyberpanel_full.htaccess"
    ));

/* ================================================================== */
/*  Execution correctness tests                                        */
/* ================================================================== */

TEST_F(CompatV2Test, WordPressIfModule_HeadersApplied) {
    std::string content = read_file("wordpress_ifmodule.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "wp_ifmod");
    ASSERT_NE(head, nullptr);

    /* Execute all directives including IfModule children */
    for (const htaccess_directive_t *d = head; d; d = d->next) {
        if (d->type == DIR_IFMODULE) {
            for (const htaccess_directive_t *c = d->data.ifmodule.children;
                 c; c = c->next) {
                exec_header(session_.handle(), c);
            }
        }
    }

    EXPECT_EQ(session_.get_response_header("X-Powered-By"), "WordPress");
    EXPECT_EQ(session_.get_response_header("X-Frame-Options"), "SAMEORIGIN");
    EXPECT_EQ(session_.get_response_header("X-Content-Type-Options"), "nosniff");

    htaccess_directives_free(head);
}

TEST_F(CompatV2Test, SecurityHeaders_AllApplied) {
    std::string content = read_file("security_headers.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "sec_hdr");
    ASSERT_NE(head, nullptr);

    for (const htaccess_directive_t *d = head; d; d = d->next)
        exec_header(session_.handle(), d);

    EXPECT_EQ(session_.get_response_header("X-Frame-Options"), "DENY");
    EXPECT_EQ(session_.get_response_header("X-Content-Type-Options"), "nosniff");
    EXPECT_EQ(session_.get_response_header("X-XSS-Protection"), "1");
    EXPECT_EQ(session_.get_response_header("Referrer-Policy"), "no-referrer");
    /* X-Powered-By should be unset */
    EXPECT_TRUE(session_.get_response_header("X-Powered-By").empty());

    htaccess_directives_free(head);
}

TEST_F(CompatV2Test, Apache24Require_ParseTypes) {
    std::string content = read_file("apache24_require.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "req24");
    ASSERT_NE(head, nullptr);

    EXPECT_TRUE(has_type(head, DIR_REQUIRE_ALL_GRANTED));
    EXPECT_TRUE(has_type(head, DIR_REQUIRE_IP));
    EXPECT_TRUE(has_type(head, DIR_REQUIRE_NOT_IP));

    htaccess_directives_free(head);
}

TEST_F(CompatV2Test, LaravelOptions_ExecutionCorrect) {
    std::string content = read_file("laravel_options.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "laravel");
    ASSERT_NE(head, nullptr);

    for (const htaccess_directive_t *d = head; d; d = d->next) {
        switch (d->type) {
        case DIR_OPTIONS:
            exec_options(session_.handle(), d);
            break;
        case DIR_ADD_TYPE:
            exec_add_type(session_.handle(), d, "/test.json");
            break;
        case DIR_FORCE_TYPE:
            exec_force_type(session_.handle(), d);
            break;
        default:
            break;
        }
    }

    /* Options: -Indexes, +FollowSymLinks, -MultiViews */
    EXPECT_EQ(session_.get_dir_option("Indexes"), 0);
    EXPECT_EQ(session_.get_dir_option("FollowSymLinks"), 1);
    EXPECT_EQ(session_.get_dir_option("MultiViews"), 0);

    /* ForceType overrides Content-Type */
    EXPECT_EQ(session_.get_response_header("Content-Type"), "text/html");

    htaccess_directives_free(head);
}

/* ================================================================== */
/*  CyberPanel full sample test (Task 25.3)                            */
/* ================================================================== */

TEST_F(CompatV2Test, CyberPanelFull_AllDirectivesRecognized) {
    std::string content = read_file("cyberpanel_full.htaccess");
    htaccess_directive_t *head = htaccess_parse(
        content.c_str(), content.size(), "cyberpanel");
    ASSERT_NE(head, nullptr);

    /* Verify all expected directive types are present */
    EXPECT_TRUE(has_type(head, DIR_OPTIONS));
    EXPECT_TRUE(has_type(head, DIR_HEADER_ALWAYS_SET));
    EXPECT_TRUE(has_type(head, DIR_EXPIRES_ACTIVE));
    EXPECT_TRUE(has_type(head, DIR_EXPIRES_DEFAULT));
    EXPECT_TRUE(has_type(head, DIR_REQUIRE_ALL_GRANTED));
    EXPECT_TRUE(has_type(head, DIR_AUTH_TYPE));
    EXPECT_TRUE(has_type(head, DIR_AUTH_NAME));
    EXPECT_TRUE(has_type(head, DIR_AUTH_USER_FILE));
    EXPECT_TRUE(has_type(head, DIR_REQUIRE_VALID_USER));
    EXPECT_TRUE(has_type(head, DIR_DIRECTORY_INDEX));
    EXPECT_TRUE(has_type(head, DIR_ADD_TYPE));
    EXPECT_TRUE(has_type(head, DIR_FORCE_TYPE));
    EXPECT_TRUE(has_type(head, DIR_ADD_ENCODING));
    EXPECT_TRUE(has_type(head, DIR_ADD_CHARSET));
    EXPECT_TRUE(has_type(head, DIR_BRUTE_FORCE_PROTECTION));
    EXPECT_TRUE(has_type(head, DIR_BRUTE_FORCE_X_FORWARDED_FOR));
    EXPECT_TRUE(has_type(head, DIR_BRUTE_FORCE_WHITELIST));
    EXPECT_TRUE(has_type(head, DIR_BRUTE_FORCE_PROTECT_PATH));

    /* Execute a subset and verify */
    for (const htaccess_directive_t *d = head; d; d = d->next) {
        switch (d->type) {
        case DIR_OPTIONS:
            exec_options(session_.handle(), d);
            break;
        case DIR_HEADER_ALWAYS_SET:
            exec_header(session_.handle(), d);
            break;
        case DIR_FORCE_TYPE:
            exec_force_type(session_.handle(), d);
            break;
        default:
            break;
        }
    }

    EXPECT_EQ(session_.get_dir_option("Indexes"), 0);
    EXPECT_EQ(session_.get_dir_option("FollowSymLinks"), 1);
    EXPECT_EQ(session_.get_response_header("X-Frame-Options"), "SAMEORIGIN");
    EXPECT_EQ(session_.get_response_header("Content-Type"), "text/html");

    htaccess_directives_free(head);
}
