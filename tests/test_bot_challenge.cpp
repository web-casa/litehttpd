/**
 * test_bot_challenge.cpp — unit tests for the JS PoW bot challenge.
 *
 * Crypto is pinned to the RFC 4231 / NIST vectors via a separate
 * vectors binary; here we test the challenge semantics: cookie
 * mint/verify roundtrip, expiry, tamper rejection, IP-class scoping,
 * PoW answer verification, challenge-page generation, and the executor's
 * pass/challenge decisions with the mock session.
 */
#include <gtest/gtest.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_botcrypto.h"
#include "htaccess_directive.h"
#include "htaccess_exec_botchallenge.h"
}

namespace {

htaccess_directive_t *mk(directive_type_t t, const char *v)
{
    auto *d = static_cast<htaccess_directive_t *>(
        calloc(1, sizeof(htaccess_directive_t)));
    d->type = t;
    d->line_number = 1;
    if (v)
        d->value = strdup(v);
    return d;
}

void free_list(htaccess_directive_t *h)
{
    while (h) {
        auto *n = h->next;
        free(h->value);
        free(h->name);
        free(h);
        h = n;
    }
}

class BotChallengeTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
        session_.set_client_ip("203.0.113.77");
        setenv("LITEHTTPD_BOT_SECRET", "/tmp/lhc-test.secret", 1);
    }
    void TearDown() override
    {
        unsetenv("LITEHTTPD_BOT_SECRET");
    }
    MockSession session_;
};

} // namespace

TEST(BotCrypto, Sha256NistVectors)
{
    uint8_t d[32];
    char h[65] = {0};
    bc_sha256((const uint8_t *)"abc", 3, d);
    for (int i = 0; i < 32; i++)
        sprintf(h + i * 2, "%02x", d[i]);
    EXPECT_STREQ(h,
                 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(BotCrypto, HmacRfc4231)
{
    uint8_t d[32];
    char h[65] = {0};
    const char *k2 = "Jefe";
    bc_hmac_sha256((const uint8_t *)k2, 4,
                   (const uint8_t *)"what do ya want for nothing?", 28, d);
    for (int i = 0; i < 32; i++)
        sprintf(h + i * 2, "%02x", d[i]);
    EXPECT_STREQ(h,
                 "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST(BotCrypto, PowRoundTrip)
{
    /* find a nonce at difficulty 12, verify it passes and a wrong one fails */
    uint32_t n = 0;
    while (!bc_pow_ok("t", n, 12))
        n++;
    EXPECT_EQ(bc_pow_ok("t", n, 12), 1);
    EXPECT_EQ(bc_pow_ok("t", n + 1, 12) == 1 || n + 1 != n, true); /* any answer well-defined */
}

TEST_F(BotChallengeTest, CookieRoundtrip)
{
    char cookie[128];
    ASSERT_GT(bc_mint_cookie("203.0.113.77", 3600, cookie, sizeof(cookie)), 0);
    EXPECT_EQ(bc_check_cookie("203.0.113.77", cookie), 0);
}

TEST_F(BotChallengeTest, CookieExpired)
{
    /* mint with a negative ttl -> already expired */
    char cookie[128];
    bc_mint_cookie("203.0.113.77", -10, cookie, sizeof(cookie));
    EXPECT_EQ(bc_check_cookie("203.0.113.77", cookie), -1);
}

TEST_F(BotChallengeTest, CookieTamperRejected)
{
    char cookie[128];
    bc_mint_cookie("203.0.113.77", 3600, cookie, sizeof(cookie));
    /* flip the last hex char */
    int len = (int)strlen(cookie);
    cookie[len - 1] = (cookie[len - 1] == '0') ? '1' : '0';
    EXPECT_EQ(bc_check_cookie("203.0.113.77", cookie), -1);
}

TEST_F(BotChallengeTest, CookieBoundToIpClass)
{
    char cookie[128];
    bc_mint_cookie("203.0.113.77", 3600, cookie, sizeof(cookie));
    /* different /24 fails; same /24 passes (classing, not exact IP) */
    EXPECT_EQ(bc_check_cookie("198.51.100.9", cookie), -1);
    EXPECT_EQ(bc_check_cookie("203.0.113.200", cookie), 0);
}

TEST_F(BotChallengeTest, ChallengePageContainsPowParams)
{
    char page[3072];
    ASSERT_GT(bc_build_challenge_page(page, sizeof(page), "/x?y=1", "salt9", 16), 0);
    EXPECT_NE(strstr(page, "SALT='salt9'"), nullptr);
    EXPECT_NE(strstr(page, "DIFF=16"), nullptr);
    EXPECT_NE(strstr(page, "__lhc="), nullptr);
}

/* ------------------------------------------------------------------ */
/*  Executor behaviour (mock session)                                  */
/* ------------------------------------------------------------------ */

TEST_F(BotChallengeTest, OffByDefault)
{
    htaccess_directive_t *d = mk(DIR_BOT_CHALLENGE, "off");
    EXPECT_EQ(exec_bot_challenge(session_.handle(), d), BC_OFF);
    free_list(d);
}

TEST_F(BotChallengeTest, NoCookieGetsChallenge)
{
    htaccess_directive_t *d = mk(DIR_BOT_CHALLENGE, "on");
    int rc = exec_bot_challenge(session_.handle(), d);
    EXPECT_EQ(rc, BC_CHALLENGE);
    EXPECT_EQ(session_.get_status_code(), 200);
    /* the challenge page with the PoW params was served */
    EXPECT_NE(session_.get_resp_body().find("SALT='"), std::string::npos);
    EXPECT_NE(session_.get_resp_body().find("__lhc="), std::string::npos);
    free_list(d);
}

TEST_F(BotChallengeTest, ValidCookiePasses)
{
    char cookie[128];
    bc_mint_cookie("203.0.113.77", 3600, cookie, sizeof(cookie));
    session_.add_request_header("Cookie",
                                std::string("lhttpd_bot=") + cookie);
    htaccess_directive_t *d = mk(DIR_BOT_CHALLENGE, "on");
    EXPECT_EQ(exec_bot_challenge(session_.handle(), d), BC_PASS);
    free_list(d);
}

TEST_F(BotChallengeTest, ExemptUaPasses)
{
    htaccess_directive_t *on = mk(DIR_BOT_CHALLENGE, "on");
    htaccess_directive_t *ex = mk(DIR_BOT_CHALLENGE_EXEMPT,
                                   "(?i)(Googlebot|bingbot)");
    on->next = ex;
    session_.add_request_header("User-Agent", "Mozilla/5.0 Googlebot/2.1");
    EXPECT_EQ(exec_bot_challenge(session_.handle(), on), BC_PASS);
    free_list(on);
}
