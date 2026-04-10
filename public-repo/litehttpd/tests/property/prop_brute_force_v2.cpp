/**
 * prop_brute_force_v2.cpp - Properties 43-45: BruteForce v2 enhancements
 *
 * Property 43: XFF IP parsing
 * Property 44: Whitelist exemption
 * Property 45: Protect path scope
 *
 * Validates: Requirements 16.1-16.6
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <cstring>
#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_brute_force.h"
#include "htaccess_directive.h"
#include "htaccess_shm.h"
}

static htaccess_directive_t *make_bf(directive_type_t type)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = type;
    return d;
}

static htaccess_directive_t *build_bf_chain(int attempts)
{
    auto *d1 = make_bf(DIR_BRUTE_FORCE_PROTECTION);
    d1->data.brute_force.enabled = 1;
    auto *d2 = make_bf(DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS);
    d2->data.brute_force.allowed_attempts = attempts;
    auto *d3 = make_bf(DIR_BRUTE_FORCE_WINDOW);
    d3->data.brute_force.window_sec = 300;
    auto *d4 = make_bf(DIR_BRUTE_FORCE_ACTION);
    d4->data.brute_force.action = BF_ACTION_BLOCK;
    d1->next = d2; d2->next = d3; d3->next = d4;
    return d1;
}

static void append_dir(htaccess_directive_t *head, htaccess_directive_t *d)
{
    htaccess_directive_t *t = head;
    while (t->next) t = t->next;
    t->next = d;
}

static rc::Gen<std::string> genIP()
{
    return rc::gen::map(
        rc::gen::tuple(
            rc::gen::inRange(1, 224),
            rc::gen::inRange(0, 256),
            rc::gen::inRange(0, 256),
            rc::gen::inRange(1, 255)),
        [](std::tuple<int,int,int,int> t) {
            return std::to_string(std::get<0>(t)) + "." +
                   std::to_string(std::get<1>(t)) + "." +
                   std::to_string(std::get<2>(t)) + "." +
                   std::to_string(std::get<3>(t));
        });
}

class BFV2PropFixture : public ::testing::Test {
public:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
        session_.set_method("POST"); /* BF only counts POST requests */
        shm_destroy();
        shm_init(nullptr, 1024);
    }
    void TearDown() override { shm_destroy(); }
protected:
    MockSession session_;
};

/* Property 43: XFF enabled uses forwarded IP */
RC_GTEST_FIXTURE_PROP(BFV2PropFixture, XFFEnabledUsesForwardedIP, ())
{
    auto xff_ip = *genIP();
    auto direct_ip = *genIP();
    RC_PRE(xff_ip != direct_ip);

    auto *chain = build_bf_chain(1); /* threshold=1, blocks on 2nd attempt */
    auto *xff = make_bf(DIR_BRUTE_FORCE_X_FORWARDED_FOR);
    xff->data.brute_force.enabled = 1;
    append_dir(chain, xff);

    session_.add_request_header("X-Forwarded-For", xff_ip);

    /* First attempt from XFF IP */
    exec_brute_force(session_.handle(), chain, direct_ip.c_str(), 1);
    /* Second attempt should block (tracked by XFF IP) */
    int rc = exec_brute_force(session_.handle(), chain, direct_ip.c_str(), 1);
    RC_ASSERT(rc == LSI_ERROR);

    htaccess_directives_free(chain);
}

/* Property 44: Whitelisted IP never blocked */
RC_GTEST_FIXTURE_PROP(BFV2PropFixture, WhitelistedIPNeverBlocked, ())
{
    auto *chain = build_bf_chain(1);
    auto *wl = make_bf(DIR_BRUTE_FORCE_WHITELIST);
    wl->value = strdup("10.0.0.0/8");
    append_dir(chain, wl);

    /* Generate an IP in 10.x.x.x range */
    auto b = *rc::gen::inRange(0, 256);
    auto c = *rc::gen::inRange(0, 256);
    auto d = *rc::gen::inRange(1, 255);
    std::string ip = "10." + std::to_string(b) + "." +
                     std::to_string(c) + "." + std::to_string(d);

    /* Many attempts should all pass */
    for (int i = 0; i < 5; i++) {
        int rc = exec_brute_force(session_.handle(), chain, ip.c_str(), 1);
        RC_ASSERT(rc == LSI_OK);
    }

    htaccess_directives_free(chain);
}

/* Property 45: Only protected paths trigger tracking */
RC_GTEST_FIXTURE_PROP(BFV2PropFixture, OnlyProtectedPathsTracked, ())
{
    auto paths = std::vector<std::string>{
        "/wp-login.php", "/admin", "/xmlrpc.php"};
    auto protIdx = *rc::gen::inRange((size_t)0, paths.size());

    auto *chain = build_bf_chain(1);
    auto *pp = make_bf(DIR_BRUTE_FORCE_PROTECT_PATH);
    pp->value = strdup(paths[protIdx].c_str());
    append_dir(chain, pp);

    /* Request to unprotected path should always pass */
    session_.set_request_uri("/safe-page.html");
    for (int i = 0; i < 5; i++) {
        int rc = exec_brute_force(session_.handle(), chain, "1.2.3.4", 1);
        RC_ASSERT(rc == LSI_OK);
    }

    htaccess_directives_free(chain);
}
