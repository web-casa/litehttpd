/**
 * prop_brute_force.cpp - Property-based tests for brute force protection
 *
 * Feature: ols-htaccess-module
 *
 * Property 23: 暴力破解防护触发
 *
 * Verifies that the N+1th failed attempt within the window triggers
 * protection, and attempts outside the window are not counted.
 *
 * **Validates: Requirements 12.2, 12.3, 12.4, 12.5**
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <cstring>
#include <cstdlib>
#include <string>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_brute_force.h"
#include "htaccess_directive.h"
#include "htaccess_shm.h"
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_bf_dir(directive_type_t type)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = type;
    d->line_number = 1;
    d->next = nullptr;
    return d;
}

static void free_dir_list(htaccess_directive_t *head)
{
    while (head) {
        htaccess_directive_t *next = head->next;
        free(head->name);
        free(head->value);
        free(head);
        head = next;
    }
}

/**
 * Build a brute force directive chain with given parameters.
 */
static htaccess_directive_t *build_bf_directives(int allowed_attempts,
                                                  int window_sec,
                                                  bf_action_t action)
{
    auto *d1 = make_bf_dir(DIR_BRUTE_FORCE_PROTECTION);
    d1->data.brute_force.enabled = 1;

    auto *d2 = make_bf_dir(DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS);
    d2->data.brute_force.allowed_attempts = allowed_attempts;

    auto *d3 = make_bf_dir(DIR_BRUTE_FORCE_WINDOW);
    d3->data.brute_force.window_sec = window_sec;

    auto *d4 = make_bf_dir(DIR_BRUTE_FORCE_ACTION);
    d4->data.brute_force.action = action;

    d1->next = d2;
    d2->next = d3;
    d3->next = d4;

    return d1;
}

/**
 * Generate a random IPv4 address string.
 */
static rc::Gen<std::string> genIpString()
{
    return rc::gen::map(
        rc::gen::tuple(
            rc::gen::inRange(1, 255),
            rc::gen::inRange(0, 256),
            rc::gen::inRange(0, 256),
            rc::gen::inRange(1, 255)
        ),
        [](const std::tuple<int, int, int, int> &t) {
            return std::to_string(std::get<0>(t)) + "." +
                   std::to_string(std::get<1>(t)) + "." +
                   std::to_string(std::get<2>(t)) + "." +
                   std::to_string(std::get<3>(t));
        });
}

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class BruteForcePropertyFixture : public ::testing::Test {
public:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
        session_.set_method("POST"); /* BF only counts POST requests */
        shm_destroy();
        shm_init(nullptr, 1024);
    }

    void TearDown() override
    {
        shm_destroy();
    }

protected:
    MockSession session_;
};

/* ------------------------------------------------------------------ */
/*  Property 23: 暴力破解防护触发                                      */
/*                                                                     */
/*  For any client IP, allowed attempts N, and window W:               */
/*  - The N+1th attempt within window W triggers protection (403)      */
/*  - Attempts outside the window are not counted                      */
/*                                                                     */
/*  **Validates: Requirements 12.2, 12.3, 12.4, 12.5**               */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(BruteForcePropertyFixture,
                      NPlus1AttemptTriggersBlock,
                      ())
{
    /* Generate random parameters within reasonable ranges */
    int allowed = *rc::gen::inRange(1, 20);
    int window = *rc::gen::inRange(60, 600);
    std::string ip = *genIpString();

    auto *dirs = build_bf_directives(allowed, window, BF_ACTION_BLOCK);

    /* Simulate N attempts — all should return LSI_OK */
    for (int i = 0; i < allowed; i++) {
        session_.reset();
        session_.set_method("POST");
        session_.set_client_ip(ip);
        session_.set_status_code(200);
        int result = exec_brute_force(session_.handle(), dirs, ip.c_str(), 1);
        RC_ASSERT(result == LSI_OK);
    }

    /* The N+1th attempt should trigger block (403) */
    session_.reset();
    session_.set_method("POST");
    session_.set_client_ip(ip);
    session_.set_status_code(200);
    int result = exec_brute_force(session_.handle(), dirs, ip.c_str(), 1);
    RC_ASSERT(result == LSI_ERROR);
    RC_ASSERT(session_.get_status_code() == 403);

    free_dir_list(dirs);
}

RC_GTEST_FIXTURE_PROP(BruteForcePropertyFixture,
                      WindowExpiryResetsCount,
                      ())
{
    /* Generate random parameters */
    int allowed = *rc::gen::inRange(2, 10);
    int window = *rc::gen::inRange(60, 300);
    std::string ip = *genIpString();

    auto *dirs = build_bf_directives(allowed, window, BF_ACTION_BLOCK);

    /* Simulate (allowed - 1) attempts — should all pass */
    for (int i = 0; i < allowed - 1; i++) {
        session_.reset();
        session_.set_method("POST");
        session_.set_client_ip(ip);
        session_.set_status_code(200);
        exec_brute_force(session_.handle(), dirs, ip.c_str(), 1);
    }

    /* Manually expire the record by setting first_attempt to the past */
    brute_force_record_t *rec = shm_get_record(ip.c_str());
    RC_ASSERT(rec != nullptr);
    rec->first_attempt = time(NULL) - window - 1;

    /* Next attempt should start a new window (count=1), not trigger block */
    session_.reset();
    session_.set_method("POST");
    session_.set_client_ip(ip);
    session_.set_status_code(200);
    int result = exec_brute_force(session_.handle(), dirs, ip.c_str(), 1);
    RC_ASSERT(result == LSI_OK);
    RC_ASSERT(session_.get_status_code() == 200);

    /* Verify the record was reset to count=1 */
    rec = shm_get_record(ip.c_str());
    RC_ASSERT(rec != nullptr);
    RC_ASSERT(rec->attempt_count == 1);

    free_dir_list(dirs);
}
