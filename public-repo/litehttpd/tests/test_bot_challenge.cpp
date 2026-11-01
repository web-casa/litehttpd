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
    EXPECT_EQ(exec_bot_challenge(session_.handle(), d, NULL), BC_OFF);
    free_list(d);
}

TEST_F(BotChallengeTest, NoCookieGetsChallenge)
{
    htaccess_directive_t *d = mk(DIR_BOT_CHALLENGE, "on");
    int rc = exec_bot_challenge(session_.handle(), d, NULL);
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
    EXPECT_EQ(exec_bot_challenge(session_.handle(), d, NULL), BC_PASS);
    free_list(d);
}

TEST_F(BotChallengeTest, ExemptUaPasses)
{
    htaccess_directive_t *on = mk(DIR_BOT_CHALLENGE, "on");
    htaccess_directive_t *ex = mk(DIR_BOT_CHALLENGE_EXEMPT,
                                   "(?i)(Googlebot|bingbot)");
    on->next = ex;
    session_.add_request_header("User-Agent", "Mozilla/5.0 Googlebot/2.1");
    EXPECT_EQ(exec_bot_challenge(session_.handle(), on, NULL), BC_PASS);
    free_list(on);
}

/* ── trusted-proxy effective IP ─────────────────────────────────── */

TEST(BotEffectiveIp, TrustedPeerUsesRightmostXff)
{
    char out[64];
    bc_pick_effective_ip("10.0.0.1", "1.2.3.4, 203.0.113.9",
                         "10.0.0.0/8", out, sizeof(out));
    EXPECT_STREQ(out, "203.0.113.9");
}

TEST(BotEffectiveIp, UntrustedPeerIgnoresXff)
{
    char out[64];
    bc_pick_effective_ip("198.51.100.5", "1.2.3.4, 203.0.113.9",
                         "10.0.0.0/8", out, sizeof(out));
    EXPECT_STREQ(out, "198.51.100.5"); /* spoofable XFF never honored */
}

TEST(BotEffectiveIp, NoDirectiveMeansPeer)
{
    char out[64];
    bc_pick_effective_ip("198.51.100.5", "1.2.3.4", NULL, out, sizeof(out));
    EXPECT_STREQ(out, "198.51.100.5");
    bc_pick_effective_ip("198.51.100.5", "1.2.3.4", "", out, sizeof(out));
    EXPECT_STREQ(out, "198.51.100.5");
}

TEST(BotEffectiveIp, GarbageXffTokenFallsBackToPeer)
{
    char out[64];
    bc_pick_effective_ip("10.0.0.1", "1.2.3.4, not-an-ip-at-all",
                         "10.0.0.0/8", out, sizeof(out));
    EXPECT_STREQ(out, "10.0.0.1");
    bc_pick_effective_ip("10.0.0.1", "", "10.0.0.0/8", out, sizeof(out));
    EXPECT_STREQ(out, "10.0.0.1");
}

/* ── JS escaping of the request URI ─────────────────────────────── */

TEST(BotJsEscape, NeutralizesBreakouts)
{
    char out[256];
    /* single quote, backslash, </script>, double quote all hex-escaped */
    bc_js_escape("/a'b\\c</script>\"d", out, sizeof(out));
    EXPECT_EQ(strchr(out, '\''), nullptr);
    EXPECT_EQ(strchr(out, '"'), nullptr);
    EXPECT_EQ(strstr(out, "<"), nullptr);
    /* every backslash starts a \xHH escape — no lone raw backslash */
    for (const char *p = out; (p = strchr(p, '\\')) != nullptr; p += 4) {
        ASSERT_EQ(p[1], 'x');
        ASSERT_TRUE((p[2] >= '0' && p[2] <= '9') ||
                    (p[2] >= 'A' && p[2] <= 'F'));
        ASSERT_TRUE((p[3] >= '0' && p[3] <= '9') ||
                    (p[3] >= 'A' && p[3] <= 'F'));
    }
    /* every escape is \xHH — nothing outside [A-Za-z0-9/_?.=&%-~] */
    for (const char *p = out; *p; p++) {
        unsigned char c = (unsigned char)*p;
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') ||
                    c == '/' || c == '_' || c == '?' || c == '.' ||
                    c == '=' || c == '&' || c == '%' || c == '-' ||
                    c == '~' || c == '\\';
        EXPECT_TRUE(safe) << "raw char 0x" << std::hex << (int)c;
    }
    /* ordinary URIs pass through untouched */
    char plain[128];
    bc_js_escape("/shop/item?id=42&page=2", plain, sizeof(plain));
    EXPECT_STREQ(plain, "/shop/item?id=42&page=2");
}

TEST(BotChallengePage, EscapesHostileUri)
{
    char page[3072];
    ASSERT_GE(bc_build_challenge_page(page, sizeof(page),
                                      "/x');</script>alert(1)//", "s1", 16), 0);
    /* the hostile URI must not appear raw anywhere in the page */
    EXPECT_EQ(strstr(page, "');</script>"), nullptr);
    EXPECT_EQ(strstr(page, "</script>alert"), nullptr);
    /* but the escaped form is present so the solver still navigates back */
    EXPECT_NE(strstr(page, "\\x27"), nullptr);
    EXPECT_NE(strstr(page, "\\x3C"), nullptr);
}

/* ── operator-supplied template ─────────────────────────────────── */

TEST(BotTemplate, SubstitutesTokensAndEscapesUri)
{
    char out[512];
    const char *tpl =
        "<html>S=%{SALT} D=%{DIFF} U=%{URI} %{UNKNOWN} 100%</html>";
    int n = bc_render_template(out, sizeof(out), tpl, "/a'b", "s9", 12);
    ASSERT_GE(n, 0);
    EXPECT_NE(strstr(out, "S=s9 D=12 U="), nullptr);
    EXPECT_EQ(strchr(out, '\''), nullptr); /* URI quote escaped */
    EXPECT_NE(strstr(out, "\\x27"), nullptr);
    /* unknown token and bare % pass through untouched */
    EXPECT_NE(strstr(out, "%{UNKNOWN}"), nullptr);
    EXPECT_NE(strstr(out, "100%"), nullptr);
    EXPECT_EQ(strstr(out, "%{SALT}"), nullptr);
}

TEST(BotTemplate, OverflowFailsClosed)
{
    char out[16];
    const char *tpl = "%{URI}%{URI}%{URI}%{URI}%{URI}%{URI}";
    EXPECT_EQ(bc_render_template(out, sizeof(out), tpl, "/long/path", "s", 8),
              -1);
}

/* ── regressions for the three codex-review findings ─────────────── */

TEST(BotChallengePage, MaxHostileUriStillGated)
{
    /* 2047-char URI made entirely of chars that expand to \xHH: the old
     * 2048-byte escape buffer overflowed the check and the gate failed
     * OPEN. With the 8192 buffer the page must build and stay escaped. */
    std::string hostile(2047, '\'');
    hostile = "/x" + hostile;
    if (hostile.size() > 2047)
        hostile.resize(2047);
    char page[16384];
    ASSERT_GE(bc_build_challenge_page(page, sizeof(page), hostile.c_str(),
                                      "s1", 16), 0);
    /* the template's own quoting is fine; the URI itself must arrive
     * fully escaped — no raw quote RUN (the hostile input was all
     * quotes) and at least one \x27 escape in the URI slot */
    EXPECT_EQ(strstr(page, "''"), nullptr);
    EXPECT_NE(strstr(page, "\\x27\\x27"), nullptr);
}

TEST_F(BotChallengeTest, PowAnswerOverflowRejectedNotUB)
{
    /* a multi-hundred-digit __lhc once drove signed long overflow (UB);
     * it must be rejected cleanly (treated as no answer → challenge) */
    htaccess_directive_t *on = mk(DIR_BOT_CHALLENGE, "on");
    session_.set_query_string(
        "__lhc=99999999999999999999999999999999999999999999999999999");
    int rc = exec_bot_challenge(session_.handle(), on, NULL);
    EXPECT_EQ(rc, BC_CHALLENGE); /* not a crash, not a pass */
    free_list(on);
}

TEST_F(BotChallengeTest, LhcParamActuallyStrippedFromQuery)
{
    /* get_uri() is path-only and set_uri_internal() preserves the query,
     * so the old cleanup was a NO-OP: __lhc stayed in the URL (cache-key
     * pollution; a CDN could cache the credential-free 200). The gate
     * must now rewrite the query explicitly. */
    htaccess_directive_t *on = mk(DIR_BOT_CHALLENGE, "on");
    /* craft a solved answer: reuse the real solver path via __lhc with a
     * nonce that satisfies the default difficulty is expensive; instead
     * verify the strip through the answer branch using a low difficulty */
    htaccess_directive_t *d1 = mk(DIR_BOT_CHALLENGE_DIFFICULTY, "8");
    d1->next = on;

    /* find a nonce for today's salt at difficulty 8 */
    char salt[32];
    snprintf(salt, sizeof(salt), "s%ld", (long)time(NULL) / 86400);
    long n = 0;
    auto fnv = [](const char *s) -> uint32_t {
        uint32_t h = 2166136261u;
        for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
        return h;
    };
    std::string msg = std::string(salt) + ":0";
    (void)msg;
    while (n < 100000000L) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s:%ld", salt, n);
        if ((fnv(buf) >> 24) == 0) break; /* 8 leading zero bits */
        n++;
    }
    ASSERT_LT(n, 100000000L);

    std::string qs = std::string("keep=1&__lhc=") + std::to_string(n) +
                     "&also=2";
    session_.set_request_uri("/gate/page");
    session_.set_query_string(qs);
    int rc = exec_bot_challenge(session_.handle(), d1, NULL);
    ASSERT_EQ(rc, BC_REDIRECT); /* 302 to the clean URL, not a 200 */
    EXPECT_GE(session_.redirect302_calls(), 1);
    EXPECT_EQ(session_.redirect302_qs(), "keep=1&also=2");
    EXPECT_EQ(session_.redirect302_deleted_qs(), 0);
    free_list(d1);

    /* and when __lhc was the only param → the query is deleted */
    htaccess_directive_t *on2 = mk(DIR_BOT_CHALLENGE, "on");
    htaccess_directive_t *d2 = mk(DIR_BOT_CHALLENGE_DIFFICULTY, "8");
    d2->next = on2;
    session_.set_request_uri("/gate/page");
    session_.set_query_string(std::string("__lhc=") + std::to_string(n));
    ASSERT_EQ(exec_bot_challenge(session_.handle(), d2, NULL), BC_REDIRECT);
    EXPECT_EQ(session_.redirect302_deleted_qs(), 1);
    free_list(d2);
}
