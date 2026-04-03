/**
 * test_brute_force_v2.cpp - Unit tests for BruteForce v2 enhancements
 *
 * Tests XFF processing, whitelist, and protect path features.
 */
#include <gtest/gtest.h>
#include <cstring>
#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_brute_force.h"
#include "htaccess_parser.h"
#include "htaccess_directive.h"
#include "htaccess_shm.h"
}

static htaccess_directive_t *make_bf_dir(directive_type_t type)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = type;
    return d;
}

/* Build a basic enabled BF chain with low threshold for testing */
static htaccess_directive_t *build_bf_base()
{
    auto *d1 = make_bf_dir(DIR_BRUTE_FORCE_PROTECTION);
    d1->data.brute_force.enabled = 1;
    auto *d2 = make_bf_dir(DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS);
    d2->data.brute_force.allowed_attempts = 2;
    auto *d3 = make_bf_dir(DIR_BRUTE_FORCE_WINDOW);
    d3->data.brute_force.window_sec = 300;
    auto *d4 = make_bf_dir(DIR_BRUTE_FORCE_ACTION);
    d4->data.brute_force.action = BF_ACTION_BLOCK;
    d1->next = d2; d2->next = d3; d3->next = d4;
    return d1;
}

class BruteForceV2Test : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
        session_.set_method("POST"); /* BF only counts POST requests */
        shm_destroy();
        shm_init(nullptr, 1024);
    }
    void TearDown() override { shm_destroy(); }
    MockSession session_;
};

/* --- Parsing tests --- */

TEST_F(BruteForceV2Test, ParseXForwardedFor) {
    const char *input = "BruteForceXForwardedFor On\n";
    auto *d = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_BRUTE_FORCE_X_FORWARDED_FOR);
    EXPECT_EQ(d->data.brute_force.enabled, 1);
    htaccess_directives_free(d);
}

TEST_F(BruteForceV2Test, ParseWhitelist) {
    const char *input = "BruteForceWhitelist 10.0.0.0/8 192.168.1.0/24\n";
    auto *d = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_BRUTE_FORCE_WHITELIST);
    EXPECT_STREQ(d->value, "10.0.0.0/8 192.168.1.0/24");
    htaccess_directives_free(d);
}

TEST_F(BruteForceV2Test, ParseProtectPath) {
    const char *input = "BruteForceProtectPath /wp-login.php\n";
    auto *d = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_BRUTE_FORCE_PROTECT_PATH);
    EXPECT_STREQ(d->value, "/wp-login.php");
    htaccess_directives_free(d);
}

/* --- XFF tests --- */

TEST_F(BruteForceV2Test, XFFEnabledUsesForwardedIP) {
    auto *base = build_bf_base();
    /* Append XFF directive */
    auto *xff = make_bf_dir(DIR_BRUTE_FORCE_X_FORWARDED_FOR);
    xff->data.brute_force.enabled = 1;
    /* Find tail */
    htaccess_directive_t *tail = base;
    while (tail->next) tail = tail->next;
    tail->next = xff;

    session_.add_request_header("X-Forwarded-For", "1.2.3.4, 5.6.7.8");

    /* First two attempts from XFF IP 1.2.3.4 */
    exec_brute_force(session_.handle(), base, "10.0.0.1", 1);
    exec_brute_force(session_.handle(), base, "10.0.0.1", 1);
    /* Third attempt should be blocked (threshold=2) */
    int rc = exec_brute_force(session_.handle(), base, "10.0.0.1", 1);
    EXPECT_EQ(rc, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    htaccess_directives_free(base);
}

TEST_F(BruteForceV2Test, XFFDisabledUsesDirectIP) {
    auto *base = build_bf_base();

    session_.add_request_header("X-Forwarded-For", "1.2.3.4");

    /* Attempts tracked by direct IP "10.0.0.1" */
    exec_brute_force(session_.handle(), base, "10.0.0.1", 1);
    exec_brute_force(session_.handle(), base, "10.0.0.1", 1);
    int rc = exec_brute_force(session_.handle(), base, "10.0.0.1", 1);
    EXPECT_EQ(rc, LSI_ERROR);

    htaccess_directives_free(base);
}

/* --- Whitelist tests --- */

TEST_F(BruteForceV2Test, WhitelistedIPBypasses) {
    auto *base = build_bf_base();
    auto *wl = make_bf_dir(DIR_BRUTE_FORCE_WHITELIST);
    wl->value = strdup("10.0.0.0/8");
    htaccess_directive_t *tail = base;
    while (tail->next) tail = tail->next;
    tail->next = wl;

    /* Even many attempts from whitelisted IP should pass */
    for (int i = 0; i < 10; i++) {
        int rc = exec_brute_force(session_.handle(), base, "10.0.0.1", 1);
        EXPECT_EQ(rc, LSI_OK);
    }

    htaccess_directives_free(base);
}

TEST_F(BruteForceV2Test, NonWhitelistedIPBlocked) {
    auto *base = build_bf_base();
    auto *wl = make_bf_dir(DIR_BRUTE_FORCE_WHITELIST);
    wl->value = strdup("10.0.0.0/8");
    htaccess_directive_t *tail = base;
    while (tail->next) tail = tail->next;
    tail->next = wl;

    exec_brute_force(session_.handle(), base, "192.168.1.1", 1);
    exec_brute_force(session_.handle(), base, "192.168.1.1", 1);
    int rc = exec_brute_force(session_.handle(), base, "192.168.1.1", 1);
    EXPECT_EQ(rc, LSI_ERROR);

    htaccess_directives_free(base);
}

/* --- Protect path tests --- */

TEST_F(BruteForceV2Test, ProtectedPathTracked) {
    auto *base = build_bf_base();
    auto *pp = make_bf_dir(DIR_BRUTE_FORCE_PROTECT_PATH);
    pp->value = strdup("/wp-login.php");
    htaccess_directive_t *tail = base;
    while (tail->next) tail = tail->next;
    tail->next = pp;

    session_.set_request_uri("/wp-login.php");

    exec_brute_force(session_.handle(), base, "1.2.3.4", 1);
    exec_brute_force(session_.handle(), base, "1.2.3.4", 1);
    int rc = exec_brute_force(session_.handle(), base, "1.2.3.4", 1);
    EXPECT_EQ(rc, LSI_ERROR);

    htaccess_directives_free(base);
}

TEST_F(BruteForceV2Test, UnprotectedPathNotTracked) {
    auto *base = build_bf_base();
    auto *pp = make_bf_dir(DIR_BRUTE_FORCE_PROTECT_PATH);
    pp->value = strdup("/wp-login.php");
    htaccess_directive_t *tail = base;
    while (tail->next) tail = tail->next;
    tail->next = pp;

    session_.set_request_uri("/index.html");

    /* Should not be tracked — always returns OK */
    for (int i = 0; i < 10; i++) {
        int rc = exec_brute_force(session_.handle(), base, "1.2.3.4", 1);
        EXPECT_EQ(rc, LSI_OK);
    }

    htaccess_directives_free(base);
}
