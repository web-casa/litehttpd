/**
 * test_exec_brute_force.cpp - Unit tests for brute force protection executor
 *
 * Tests block and throttle actions, default values, window expiry,
 * and graceful degradation when SHM is unavailable.
 *
 * Validates: Requirements 12.1, 12.2, 12.3, 12.4, 12.5, 12.6, 12.7, 12.8
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

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
static htaccess_directive_t *build_bf_directives(
    int enabled, int allowed_attempts, int window_sec,
    bf_action_t action, int throttle_ms)
{
    auto *d1 = make_bf_dir(DIR_BRUTE_FORCE_PROTECTION);
    d1->data.brute_force.enabled = enabled;

    auto *d2 = make_bf_dir(DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS);
    d2->data.brute_force.allowed_attempts = allowed_attempts;

    auto *d3 = make_bf_dir(DIR_BRUTE_FORCE_WINDOW);
    d3->data.brute_force.window_sec = window_sec;

    auto *d4 = make_bf_dir(DIR_BRUTE_FORCE_ACTION);
    d4->data.brute_force.action = action;

    auto *d5 = make_bf_dir(DIR_BRUTE_FORCE_THROTTLE_DURATION);
    d5->data.brute_force.throttle_ms = throttle_ms;

    d1->next = d2;
    d2->next = d3;
    d3->next = d4;
    d4->next = d5;

    return d1;
}

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class ExecBruteForceTest : public ::testing::Test {
protected:
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

    MockSession session_;
};

/* ------------------------------------------------------------------ */
/*  BruteForceProtection On/Off (Req 12.1)                             */
/* ------------------------------------------------------------------ */

TEST_F(ExecBruteForceTest, DisabledReturnsOk)
{
    auto *dirs = build_bf_directives(0, 3, 300, BF_ACTION_BLOCK, 1000);
    session_.set_client_ip("10.0.0.1");

    /* Even with many attempts, disabled protection should always return OK */
    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(exec_brute_force(session_.handle(), dirs, "10.0.0.1", 1), LSI_OK);
    }

    free_dir_list(dirs);
}

TEST_F(ExecBruteForceTest, EnabledTracksAttempts)
{
    auto *dirs = build_bf_directives(1, 3, 300, BF_ACTION_BLOCK, 1000);
    session_.set_client_ip("10.0.0.1");

    /* First 3 attempts should pass */
    for (int i = 0; i < 3; i++) {
        session_.set_status_code(200);
        EXPECT_EQ(exec_brute_force(session_.handle(), dirs, "10.0.0.1", 1), LSI_OK);
    }

    /* 4th attempt should be blocked */
    session_.set_status_code(200);
    EXPECT_EQ(exec_brute_force(session_.handle(), dirs, "10.0.0.1", 1), LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    free_dir_list(dirs);
}

/* ------------------------------------------------------------------ */
/*  Block action (Req 12.5)                                            */
/* ------------------------------------------------------------------ */

TEST_F(ExecBruteForceTest, BlockActionReturns403)
{
    auto *dirs = build_bf_directives(1, 2, 300, BF_ACTION_BLOCK, 1000);
    session_.set_client_ip("192.168.1.1");

    /* 2 attempts pass */
    exec_brute_force(session_.handle(), dirs, "192.168.1.1", 1);
    exec_brute_force(session_.handle(), dirs, "192.168.1.1", 1);

    /* 3rd triggers block */
    session_.set_status_code(200);
    int result = exec_brute_force(session_.handle(), dirs, "192.168.1.1", 1);
    EXPECT_EQ(result, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    free_dir_list(dirs);
}

/* ------------------------------------------------------------------ */
/*  Throttle action (Req 12.6)                                         */
/* ------------------------------------------------------------------ */

TEST_F(ExecBruteForceTest, ThrottleActionSetsEnvVar)
{
    auto *dirs = build_bf_directives(1, 2, 300, BF_ACTION_THROTTLE, 1);
    session_.set_client_ip("192.168.1.1");

    /* 2 attempts pass */
    exec_brute_force(session_.handle(), dirs, "192.168.1.1", 1);
    exec_brute_force(session_.handle(), dirs, "192.168.1.1", 1);

    /* 3rd triggers throttle — returns LSI_ERROR with 429 and sets env var */
    /* Progressive: excess=1, so 1*1=1 ms */
    session_.set_status_code(200);
    int result = exec_brute_force(session_.handle(), dirs, "192.168.1.1", 1);
    EXPECT_EQ(result, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 429);
    EXPECT_TRUE(session_.has_env_var("BF_THROTTLE_MS"));
    EXPECT_EQ(session_.get_env_var("BF_THROTTLE_MS"), "1");

    free_dir_list(dirs);
}

TEST_F(ExecBruteForceTest, ThrottleProgressiveDelay)
{
    /* allowed=2, throttle_ms=100 */
    auto *dirs = build_bf_directives(1, 2, 300, BF_ACTION_THROTTLE, 100);
    session_.set_client_ip("10.0.0.60");

    /* 2 attempts pass (count goes to 2) */
    exec_brute_force(session_.handle(), dirs, "10.0.0.60", 1);
    exec_brute_force(session_.handle(), dirs, "10.0.0.60", 1);

    /* 3rd attempt: excess=1, delay=100*1=100 */
    session_.set_status_code(200);
    exec_brute_force(session_.handle(), dirs, "10.0.0.60", 1);
    EXPECT_EQ(session_.get_env_var("BF_THROTTLE_MS"), "100");

    /* 4th attempt: excess=2, delay=100*2=200 */
    session_.set_status_code(200);
    exec_brute_force(session_.handle(), dirs, "10.0.0.60", 1);
    EXPECT_EQ(session_.get_env_var("BF_THROTTLE_MS"), "200");

    /* 5th attempt: excess=3, delay=100*3=300 */
    session_.set_status_code(200);
    exec_brute_force(session_.handle(), dirs, "10.0.0.60", 1);
    EXPECT_EQ(session_.get_env_var("BF_THROTTLE_MS"), "300");

    free_dir_list(dirs);
}

TEST_F(ExecBruteForceTest, ThrottleCapsAtMaxMs)
{
    /* Use throttle_ms=10000 with allowed=1 */
    auto *dirs = build_bf_directives(1, 1, 300, BF_ACTION_THROTTLE, 10000);
    session_.set_client_ip("10.0.0.50");

    /* 1 attempt passes */
    exec_brute_force(session_.handle(), dirs, "10.0.0.50", 1);

    /* 2nd: excess=1, delay=10000*1=10000 (under cap) */
    session_.set_status_code(200);
    exec_brute_force(session_.handle(), dirs, "10.0.0.50", 1);
    EXPECT_EQ(session_.get_env_var("BF_THROTTLE_MS"), "10000");

    /* 3rd: excess=2, delay=10000*2=20000 (under cap) */
    session_.set_status_code(200);
    exec_brute_force(session_.handle(), dirs, "10.0.0.50", 1);
    EXPECT_EQ(session_.get_env_var("BF_THROTTLE_MS"), "20000");

    /* 4th: excess=3, delay=10000*3=30000 (at cap) */
    session_.set_status_code(200);
    exec_brute_force(session_.handle(), dirs, "10.0.0.50", 1);
    EXPECT_EQ(session_.get_env_var("BF_THROTTLE_MS"), "30000");

    /* 5th: excess=4, delay=10000*4=40000 (capped to 30000) */
    session_.set_status_code(200);
    exec_brute_force(session_.handle(), dirs, "10.0.0.50", 1);
    EXPECT_EQ(session_.get_env_var("BF_THROTTLE_MS"), "30000");

    free_dir_list(dirs);
}

/* ------------------------------------------------------------------ */
/*  Log action — log only, do not block or delay                       */
/* ------------------------------------------------------------------ */

TEST_F(ExecBruteForceTest, LogActionSetsEnvVarButAllows)
{
    auto *dirs = build_bf_directives(1, 2, 300, BF_ACTION_LOG, 1000);
    session_.set_client_ip("10.0.0.99");

    /* 2 attempts pass */
    exec_brute_force(session_.handle(), dirs, "10.0.0.99", 1);
    exec_brute_force(session_.handle(), dirs, "10.0.0.99", 1);

    /* 3rd triggers log — returns OK (not blocked), sets BF_LOG env var */
    session_.set_status_code(200);
    int result = exec_brute_force(session_.handle(), dirs, "10.0.0.99", 1);
    EXPECT_EQ(result, LSI_OK);
    EXPECT_EQ(session_.get_status_code(), 200);
    EXPECT_TRUE(session_.has_env_var("BF_LOG"));
    /* BF_LOG value contains the IP and attempt count */
    std::string log_val = session_.get_env_var("BF_LOG");
    EXPECT_NE(log_val.find("10.0.0.99"), std::string::npos);
    EXPECT_NE(log_val.find("log mode"), std::string::npos);

    free_dir_list(dirs);
}

/* ------------------------------------------------------------------ */
/*  Default values (Req 12.7, 12.8)                                    */
/* ------------------------------------------------------------------ */

TEST_F(ExecBruteForceTest, DefaultAllowedAttempts10)
{
    /* Only set enabled, no attempts/window directives → defaults apply */
    auto *d1 = make_bf_dir(DIR_BRUTE_FORCE_PROTECTION);
    d1->data.brute_force.enabled = 1;

    session_.set_client_ip("10.0.0.1");

    /* 10 attempts should pass (default allowed = 10) */
    for (int i = 0; i < 10; i++) {
        session_.set_status_code(200);
        EXPECT_EQ(exec_brute_force(session_.handle(), d1, "10.0.0.1", 1), LSI_OK);
    }

    /* 11th should be blocked */
    session_.set_status_code(200);
    EXPECT_EQ(exec_brute_force(session_.handle(), d1, "10.0.0.1", 1), LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    free_dir_list(d1);
}

/* ------------------------------------------------------------------ */
/*  Window expiry resets count (Req 12.3)                              */
/* ------------------------------------------------------------------ */

TEST_F(ExecBruteForceTest, WindowExpiryResetsCount)
{
    auto *dirs = build_bf_directives(1, 3, 60, BF_ACTION_BLOCK, 1000);
    session_.set_client_ip("10.0.0.1");

    /* 2 attempts */
    exec_brute_force(session_.handle(), dirs, "10.0.0.1", 1);
    exec_brute_force(session_.handle(), dirs, "10.0.0.1", 1);

    /* Expire the window by manipulating the record */
    brute_force_record_t *rec = shm_get_record("10.0.0.1");
    ASSERT_NE(rec, nullptr);
    rec->first_attempt = time(nullptr) - 61; /* Past the 60s window */

    /* Next attempt should reset count to 1, not trigger block */
    session_.set_status_code(200);
    EXPECT_EQ(exec_brute_force(session_.handle(), dirs, "10.0.0.1", 1), LSI_OK);
    EXPECT_EQ(session_.get_status_code(), 200);

    /* Verify count was reset */
    rec = shm_get_record("10.0.0.1");
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->attempt_count, 1);

    free_dir_list(dirs);
}

/* ------------------------------------------------------------------ */
/*  SHM not initialized — graceful degradation (Req 12.8 implied)     */
/* ------------------------------------------------------------------ */

TEST_F(ExecBruteForceTest, ShmNotInitializedReturnsOk)
{
    shm_destroy(); /* Ensure SHM is not available */

    auto *dirs = build_bf_directives(1, 2, 300, BF_ACTION_BLOCK, 1000);
    session_.set_client_ip("10.0.0.1");

    /* Should return OK even with many attempts — protection disabled */
    for (int i = 0; i < 10; i++) {
        session_.set_status_code(200);
        EXPECT_EQ(exec_brute_force(session_.handle(), dirs, "10.0.0.1", 1), LSI_OK);
    }

    free_dir_list(dirs);
}

/* ------------------------------------------------------------------ */
/*  Different IPs tracked independently (Req 12.2)                     */
/* ------------------------------------------------------------------ */

TEST_F(ExecBruteForceTest, DifferentIpsTrackedIndependently)
{
    auto *dirs = build_bf_directives(1, 2, 300, BF_ACTION_BLOCK, 1000);

    /* IP A: 2 attempts */
    exec_brute_force(session_.handle(), dirs, "10.0.0.1", 1);
    exec_brute_force(session_.handle(), dirs, "10.0.0.1", 1);

    /* IP B: 1 attempt — should not be affected by IP A */
    session_.set_status_code(200);
    EXPECT_EQ(exec_brute_force(session_.handle(), dirs, "10.0.0.2", 1), LSI_OK);
    EXPECT_EQ(session_.get_status_code(), 200);

    /* IP A: 3rd attempt should be blocked */
    session_.set_status_code(200);
    EXPECT_EQ(exec_brute_force(session_.handle(), dirs, "10.0.0.1", 1), LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    /* IP B: 2nd attempt should still pass */
    session_.set_status_code(200);
    EXPECT_EQ(exec_brute_force(session_.handle(), dirs, "10.0.0.2", 1), LSI_OK);

    free_dir_list(dirs);
}

/* ------------------------------------------------------------------ */
/*  Edge cases                                                         */
/* ------------------------------------------------------------------ */

TEST_F(ExecBruteForceTest, NullSessionReturnsOk)
{
    auto *dirs = build_bf_directives(1, 2, 300, BF_ACTION_BLOCK, 1000);
    EXPECT_EQ(exec_brute_force(nullptr, dirs, "10.0.0.1", 1), LSI_OK);
    free_dir_list(dirs);
}

TEST_F(ExecBruteForceTest, NullDirectivesReturnsOk)
{
    EXPECT_EQ(exec_brute_force(session_.handle(), nullptr, "10.0.0.1", 1), LSI_OK);
}

TEST_F(ExecBruteForceTest, NullClientIpReturnsOk)
{
    auto *dirs = build_bf_directives(1, 2, 300, BF_ACTION_BLOCK, 1000);
    EXPECT_EQ(exec_brute_force(session_.handle(), dirs, nullptr, 1), LSI_OK);
    free_dir_list(dirs);
}

TEST_F(ExecBruteForceTest, NoBruteForceDirectivesReturnsOk)
{
    /* Directive list with no brute force directives */
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_HEADER_SET;
    d->name = strdup("X-Test");
    d->value = strdup("val");
    d->next = nullptr;

    session_.set_client_ip("10.0.0.1");
    EXPECT_EQ(exec_brute_force(session_.handle(), d, "10.0.0.1", 1), LSI_OK);

    free_dir_list(d);
}
