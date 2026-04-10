/**
 * test_exec_require.cpp - Unit tests for Require access control executor
 *
 * Tests Require all granted/denied, Require ip, Require not ip,
 * RequireAny/RequireAll containers, and Require vs Order/Allow/Deny precedence.
 *
 * Validates: Requirements 8.1-8.7
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <string>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_require.h"
#include "htaccess_directive.h"
#include "htaccess_parser.h"
}

/* ---- Helpers ---- */

static htaccess_directive_t *parse(const char *input) {
    return htaccess_parse(input, strlen(input), "test");
}

class RequireExecTest : public ::testing::Test {
public:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
protected:
    MockSession session_;
};

/* Require all granted — allows any IP */
TEST_F(RequireExecTest, AllGranted)
{
    const char *input = "Require all granted\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    session_.set_client_ip("10.0.0.1");
    int rc = exec_require(session_.handle(), dirs, "10.0.0.1", 0);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

/* Require all denied — denies any IP */
TEST_F(RequireExecTest, AllDenied)
{
    const char *input = "Require all denied\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "10.0.0.1", 0);
    EXPECT_EQ(rc, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    htaccess_directives_free(dirs);
}

/* Require ip — matching CIDR grants access */
TEST_F(RequireExecTest, IpMatchGrants)
{
    const char *input = "Require ip 192.168.1.0/24\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "192.168.1.50", 0);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

/* Require ip — non-matching CIDR denies */
TEST_F(RequireExecTest, IpNoMatchDenies)
{
    const char *input = "Require ip 192.168.1.0/24\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "10.0.0.1", 0);
    EXPECT_EQ(rc, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    htaccess_directives_free(dirs);
}

/* Require not ip — matching CIDR denies */
TEST_F(RequireExecTest, NotIpMatchDenies)
{
    const char *input = "Require not ip 10.0.0.0/8\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "10.0.0.1", 0);
    EXPECT_EQ(rc, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    htaccess_directives_free(dirs);
}

/* Require not ip — non-matching CIDR grants */
TEST_F(RequireExecTest, NotIpNoMatchGrants)
{
    const char *input = "Require not ip 10.0.0.0/8\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "192.168.1.1", 0);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

/* RequireAny — OR logic: one match grants */
TEST_F(RequireExecTest, RequireAnyOrLogic)
{
    const char *input =
        "<RequireAny>\n"
        "Require ip 192.168.1.0/24\n"
        "Require ip 10.0.0.0/8\n"
        "</RequireAny>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    /* Matches second rule */
    int rc = exec_require(session_.handle(), dirs, "10.0.0.5", 0);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

/* RequireAny — no match denies */
TEST_F(RequireExecTest, RequireAnyNoMatchDenies)
{
    const char *input =
        "<RequireAny>\n"
        "Require ip 192.168.1.0/24\n"
        "Require ip 10.0.0.0/8\n"
        "</RequireAny>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "172.16.0.1", 0);
    EXPECT_EQ(rc, LSI_ERROR);

    htaccess_directives_free(dirs);
}

/* RequireAll — AND logic: all must match */
TEST_F(RequireExecTest, RequireAllAndLogic)
{
    const char *input =
        "<RequireAll>\n"
        "Require all granted\n"
        "Require not ip 10.0.0.0/8\n"
        "</RequireAll>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    /* 192.168.1.1: all granted=true, not ip 10.0.0.0/8=true → granted */
    int rc = exec_require(session_.handle(), dirs, "192.168.1.1", 0);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

/* RequireAll — one fails denies */
TEST_F(RequireExecTest, RequireAllOneFailDenies)
{
    const char *input =
        "<RequireAll>\n"
        "Require all granted\n"
        "Require not ip 10.0.0.0/8\n"
        "</RequireAll>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    /* 10.0.0.1: all granted=true, not ip 10.0.0.0/8=false → denied */
    int rc = exec_require(session_.handle(), dirs, "10.0.0.1", 0);
    EXPECT_EQ(rc, LSI_ERROR);

    htaccess_directives_free(dirs);
}

/* Require takes precedence over Order/Allow/Deny */
TEST_F(RequireExecTest, RequirePrecedenceOverOrderAllowDeny)
{
    const char *input =
        "Order Deny,Allow\n"
        "Deny from all\n"
        "Require all granted\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    /* Require all granted should override Deny from all */
    int rc = exec_require(session_.handle(), dirs, "10.0.0.1", 0);
    EXPECT_EQ(rc, LSI_OK);

    /* Verify warning was logged */
    const auto &logs = mock_lsiapi::get_log_records();
    bool found_warn = false;
    for (const auto &rec : logs) {
        if (rec.level == LSI_LOG_WARN &&
            rec.message.find("Require takes precedence") != std::string::npos) {
            found_warn = true;
            break;
        }
    }
    EXPECT_TRUE(found_warn);

    htaccess_directives_free(dirs);
}

/* No Require directives — returns OK (no access control) */
TEST_F(RequireExecTest, NoRequireDirectivesAllows)
{
    const char *input = "Header set X-Test value\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "10.0.0.1", 0);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

/* Multiple Require ip — space-separated CIDRs */
TEST_F(RequireExecTest, RequireIpMultipleCidrs)
{
    const char *input = "Require ip 192.168.1.0/24 10.0.0.0/8\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "10.5.5.5", 0);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

/* ==================================================================
 *  Require ip — Apache-style prefix matching (no slash)
 * ================================================================== */

/* "Require ip 10" matches 10.x.x.x (auto /8) */
TEST_F(RequireExecTest, RequireIpPrefix_0Dots_Slash8)
{
    const char *input = "Require ip 10\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "10.255.255.255", 0);
    EXPECT_EQ(rc, LSI_OK);

    session_.reset();
    rc = exec_require(session_.handle(), dirs, "11.0.0.1", 0);
    EXPECT_EQ(rc, LSI_ERROR);

    htaccess_directives_free(dirs);
}

/* "Require ip 192.168" matches 192.168.x.x (auto /16) */
TEST_F(RequireExecTest, RequireIpPrefix_1Dot_Slash16)
{
    const char *input = "Require ip 192.168\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "192.168.5.5", 0);
    EXPECT_EQ(rc, LSI_OK);

    session_.reset();
    rc = exec_require(session_.handle(), dirs, "192.169.0.1", 0);
    EXPECT_EQ(rc, LSI_ERROR);

    htaccess_directives_free(dirs);
}

/* "Require ip 172.16.0" matches 172.16.0.x (auto /24) */
TEST_F(RequireExecTest, RequireIpPrefix_2Dots_Slash24)
{
    const char *input = "Require ip 172.16.0\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "172.16.0.42", 0);
    EXPECT_EQ(rc, LSI_OK);

    session_.reset();
    rc = exec_require(session_.handle(), dirs, "172.16.1.1", 0);
    EXPECT_EQ(rc, LSI_ERROR);

    htaccess_directives_free(dirs);
}

/* "Require ip 10.0.0.1" without slash matches exactly (auto /32) */
TEST_F(RequireExecTest, RequireIpPrefix_3Dots_Slash32)
{
    const char *input = "Require ip 10.0.0.1\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "10.0.0.1", 0);
    EXPECT_EQ(rc, LSI_OK);

    session_.reset();
    rc = exec_require(session_.handle(), dirs, "10.0.0.2", 0);
    EXPECT_EQ(rc, LSI_ERROR);

    htaccess_directives_free(dirs);
}

/* ==================================================================
 *  Require env — environment variable check
 * ================================================================== */

/* Require env — variable is set */
TEST_F(RequireExecTest, RequireEnvSet)
{
    const char *input = "Require env REDIRECT_STATUS\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_REQUIRE_ENV);
    EXPECT_STREQ(dirs->name, "REDIRECT_STATUS");

    /* Set the env var in mock session */
    lsi_session_set_env(session_.handle(), "REDIRECT_STATUS", 15, "200", 3);
    int rc = exec_require(session_.handle(), dirs, "10.0.0.1", 0);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

/* Require env — variable is NOT set → deny */
TEST_F(RequireExecTest, RequireEnvNotSet)
{
    const char *input = "Require env REDIRECT_STATUS\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "10.0.0.1", 0);
    EXPECT_EQ(rc, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    htaccess_directives_free(dirs);
}

/* ==================================================================
 *  Require ip — IPv6 support
 * ================================================================== */

/* Require ip — IPv6 CIDR match */
TEST_F(RequireExecTest, RequireIpV6Match)
{
    const char *input = "Require ip 2001:db8::/32\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "2001:db8::1", 0);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

/* Require ip — IPv6 no match */
TEST_F(RequireExecTest, RequireIpV6NoMatch)
{
    const char *input = "Require ip 2001:db8::/32\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "2001:db9::1", 0);
    EXPECT_EQ(rc, LSI_ERROR);

    htaccess_directives_free(dirs);
}

/* Require not ip — IPv6 */
TEST_F(RequireExecTest, RequireNotIpV6)
{
    const char *input = "Require not ip ::1/128\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "::1", 0);
    EXPECT_EQ(rc, LSI_ERROR);

    session_.reset();
    rc = exec_require(session_.handle(), dirs, "2001:db8::1", 0);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

/* ------------------------------------------------------------------ */
/*  RequireAny/RequireAll + Require valid-user with auth_ok            */
/* ------------------------------------------------------------------ */

TEST_F(RequireExecTest, RequireAnyValidUserGrantsWhenAuthenticated)
{
    const char *input =
        "<RequireAny>\n"
        "Require ip 192.168.1.0/24\n"
        "Require valid-user\n"
        "</RequireAny>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "10.0.0.5", 1);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

TEST_F(RequireExecTest, RequireAnyValidUserDefersWhenNotAuthenticated)
{
    const char *input =
        "<RequireAny>\n"
        "Require ip 192.168.1.0/24\n"
        "Require valid-user\n"
        "</RequireAny>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "10.0.0.5", 0);
    EXPECT_EQ(rc, LSI_OK); /* Defers to exec_auth_basic */

    htaccess_directives_free(dirs);
}

TEST_F(RequireExecTest, RequireAllValidUserGrantsWhenBothMet)
{
    const char *input =
        "<RequireAll>\n"
        "Require ip 10.0.0.0/8\n"
        "Require valid-user\n"
        "</RequireAll>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "10.0.0.5", 1);
    EXPECT_EQ(rc, LSI_OK);

    htaccess_directives_free(dirs);
}

TEST_F(RequireExecTest, RequireAllValidUserDeniesWhenNotAuthenticated)
{
    const char *input =
        "<RequireAll>\n"
        "Require ip 10.0.0.0/8\n"
        "Require valid-user\n"
        "</RequireAll>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    int rc = exec_require(session_.handle(), dirs, "10.0.0.5", 0);
    EXPECT_EQ(rc, LSI_OK); /* Defers to exec_auth_basic */

    htaccess_directives_free(dirs);
}

/* RequireAny non-Require child should be rejected at parse time */
TEST_F(RequireExecTest, RequireAnyRejectsNonRequireChildren)
{
    mock_lsiapi::reset_global_state();
    const char *input =
        "<RequireAny>\n"
        "Require ip 10.0.0.0/8\n"
        "Header set X-Bad value\n"
        "</RequireAny>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    /* The Header directive should have been discarded during parsing.
     * Only Require ip should remain as child. */
    bool found_warn = false;
    const auto &logs = mock_lsiapi::get_log_records();
    for (const auto &rec : logs) {
        if (rec.level == LSI_LOG_WARN &&
            rec.message.find("non-Require") != std::string::npos) {
            found_warn = true;
            break;
        }
    }
    EXPECT_TRUE(found_warn) << "Expected WARN about non-Require child in RequireAny";

    htaccess_directives_free(dirs);
}
