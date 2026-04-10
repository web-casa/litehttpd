/**
 * test_exec_acl.cpp - Unit tests for access control executor
 *
 * Tests Order Allow,Deny and Deny,Allow with specific examples,
 * "all" keyword, single IP, and CIDR ranges.
 *
 * Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.5, 6.6
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_acl.h"
#include "htaccess_directive.h"
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_order(acl_order_t order)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ORDER;
    d->line_number = 1;
    d->data.acl.order = order;
    d->next = nullptr;
    return d;
}

static htaccess_directive_t *make_allow(const char *value)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ALLOW_FROM;
    d->line_number = 1;
    d->value = value ? strdup(value) : nullptr;
    d->next = nullptr;
    return d;
}

static htaccess_directive_t *make_deny(const char *value)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_DENY_FROM;
    d->line_number = 1;
    d->value = value ? strdup(value) : nullptr;
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

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class ExecAclTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }

    MockSession session_;
};

/* ------------------------------------------------------------------ */
/*  Order Allow,Deny tests (Req 6.1)                                   */
/* ------------------------------------------------------------------ */

TEST_F(ExecAclTest, AllowDeny_DefaultDeniesWhenNoRules)
{
    /* Order Allow,Deny with no Allow/Deny rules → default deny */
    session_.set_client_ip("10.0.0.1");
    auto *order = make_order(ORDER_ALLOW_DENY);

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    free_dir_list(order);
}

TEST_F(ExecAclTest, AllowDeny_AllowMatchedNoDeny)
{
    /* Allow matches, no Deny → allowed */
    session_.set_client_ip("192.168.1.50");
    auto *order = make_order(ORDER_ALLOW_DENY);
    auto *allow = make_allow("192.168.1.0/24");
    order->next = allow;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_OK);
    EXPECT_EQ(session_.get_status_code(), 200);

    free_dir_list(order);
}

TEST_F(ExecAclTest, AllowDeny_AllowMatchedDenyAlsoMatched)
{
    /* Both Allow and Deny match → denied (Deny overrides) */
    session_.set_client_ip("192.168.1.50");
    auto *order = make_order(ORDER_ALLOW_DENY);
    auto *allow = make_allow("192.168.1.0/24");
    auto *deny = make_deny("192.168.1.50");
    order->next = allow;
    allow->next = deny;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    free_dir_list(order);
}

TEST_F(ExecAclTest, AllowDeny_NoAllowMatch)
{
    /* Allow doesn't match → denied (default) */
    session_.set_client_ip("10.0.0.1");
    auto *order = make_order(ORDER_ALLOW_DENY);
    auto *allow = make_allow("192.168.1.0/24");
    order->next = allow;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    free_dir_list(order);
}

/* ------------------------------------------------------------------ */
/*  Order Deny,Allow tests (Req 6.2)                                   */
/* ------------------------------------------------------------------ */

TEST_F(ExecAclTest, DenyAllow_DefaultAllowsWhenNoRules)
{
    /* Order Deny,Allow with no rules → default allow */
    session_.set_client_ip("10.0.0.1");
    auto *order = make_order(ORDER_DENY_ALLOW);

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_OK);
    EXPECT_EQ(session_.get_status_code(), 200);

    free_dir_list(order);
}

TEST_F(ExecAclTest, DenyAllow_DenyMatchedNoAllow)
{
    /* Deny matches, no Allow → denied */
    session_.set_client_ip("10.0.0.1");
    auto *order = make_order(ORDER_DENY_ALLOW);
    auto *deny = make_deny("10.0.0.0/8");
    order->next = deny;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    free_dir_list(order);
}

TEST_F(ExecAclTest, DenyAllow_DenyMatchedAllowAlsoMatched)
{
    /* Both Deny and Allow match → allowed (Allow overrides) */
    session_.set_client_ip("10.0.0.1");
    auto *order = make_order(ORDER_DENY_ALLOW);
    auto *deny = make_deny("10.0.0.0/8");
    auto *allow = make_allow("10.0.0.1");
    order->next = deny;
    deny->next = allow;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_OK);
    EXPECT_EQ(session_.get_status_code(), 200);

    free_dir_list(order);
}

TEST_F(ExecAclTest, DenyAllow_NoDenyMatch)
{
    /* Deny doesn't match → allowed (default) */
    session_.set_client_ip("192.168.1.1");
    auto *order = make_order(ORDER_DENY_ALLOW);
    auto *deny = make_deny("10.0.0.0/8");
    order->next = deny;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_OK);
    EXPECT_EQ(session_.get_status_code(), 200);

    free_dir_list(order);
}

/* ------------------------------------------------------------------ */
/*  "all" keyword tests (Req 6.5)                                      */
/* ------------------------------------------------------------------ */

TEST_F(ExecAclTest, AllowAll_AllowDeny)
{
    /* Allow from all → allowed */
    session_.set_client_ip("1.2.3.4");
    auto *order = make_order(ORDER_ALLOW_DENY);
    auto *allow = make_allow("all");
    order->next = allow;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_OK);

    free_dir_list(order);
}

TEST_F(ExecAclTest, DenyAll_AllowDeny)
{
    /* Allow from all, Deny from all → denied (Deny overrides in Allow,Deny) */
    session_.set_client_ip("1.2.3.4");
    auto *order = make_order(ORDER_ALLOW_DENY);
    auto *allow = make_allow("all");
    auto *deny = make_deny("all");
    order->next = allow;
    allow->next = deny;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    free_dir_list(order);
}

TEST_F(ExecAclTest, DenyAll_DenyAllow)
{
    /* Deny from all with no Allow → denied */
    session_.set_client_ip("1.2.3.4");
    auto *order = make_order(ORDER_DENY_ALLOW);
    auto *deny = make_deny("all");
    order->next = deny;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    free_dir_list(order);
}

TEST_F(ExecAclTest, AllowAll_DenyAll_DenyAllow)
{
    /* Deny from all, Allow from all → allowed (Allow overrides in Deny,Allow) */
    session_.set_client_ip("1.2.3.4");
    auto *order = make_order(ORDER_DENY_ALLOW);
    auto *deny = make_deny("all");
    auto *allow = make_allow("all");
    order->next = deny;
    deny->next = allow;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_OK);

    free_dir_list(order);
}

/* ------------------------------------------------------------------ */
/*  Single IP tests (Req 6.3, 6.4)                                     */
/* ------------------------------------------------------------------ */

TEST_F(ExecAclTest, SingleIpAllow)
{
    session_.set_client_ip("10.0.0.5");
    auto *order = make_order(ORDER_ALLOW_DENY);
    auto *allow = make_allow("10.0.0.5");
    order->next = allow;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_OK);

    free_dir_list(order);
}

TEST_F(ExecAclTest, SingleIpDeny)
{
    session_.set_client_ip("10.0.0.5");
    auto *order = make_order(ORDER_DENY_ALLOW);
    auto *deny = make_deny("10.0.0.5");
    order->next = deny;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    free_dir_list(order);
}

TEST_F(ExecAclTest, SingleIpNoMatch)
{
    session_.set_client_ip("10.0.0.6");
    auto *order = make_order(ORDER_ALLOW_DENY);
    auto *allow = make_allow("10.0.0.5");
    order->next = allow;

    /* Allow doesn't match → denied (default for Allow,Deny) */
    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    free_dir_list(order);
}

/* ------------------------------------------------------------------ */
/*  CIDR range tests (Req 6.3, 6.4)                                    */
/* ------------------------------------------------------------------ */

TEST_F(ExecAclTest, CidrAllowRange)
{
    session_.set_client_ip("172.16.5.100");
    auto *order = make_order(ORDER_ALLOW_DENY);
    auto *allow = make_allow("172.16.0.0/16");
    order->next = allow;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_OK);

    free_dir_list(order);
}

TEST_F(ExecAclTest, CidrDenyRange)
{
    session_.set_client_ip("172.16.5.100");
    auto *order = make_order(ORDER_DENY_ALLOW);
    auto *deny = make_deny("172.16.0.0/16");
    order->next = deny;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    free_dir_list(order);
}

TEST_F(ExecAclTest, CidrOutsideRange)
{
    session_.set_client_ip("192.168.1.1");
    auto *order = make_order(ORDER_DENY_ALLOW);
    auto *deny = make_deny("172.16.0.0/16");
    order->next = deny;

    /* IP not in deny range → allowed (default for Deny,Allow) */
    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_OK);

    free_dir_list(order);
}

/* ------------------------------------------------------------------ */
/*  403 Forbidden response (Req 6.6)                                   */
/* ------------------------------------------------------------------ */

TEST_F(ExecAclTest, DeniedSets403)
{
    session_.set_client_ip("10.0.0.1");
    session_.set_status_code(200);
    auto *order = make_order(ORDER_ALLOW_DENY);
    /* No Allow rules → default deny */

    int result = exec_access_control(session_.handle(), order);
    EXPECT_EQ(result, LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    free_dir_list(order);
}

/* ------------------------------------------------------------------ */
/*  Edge cases                                                         */
/* ------------------------------------------------------------------ */

TEST_F(ExecAclTest, NullSessionReturnsOk)
{
    auto *order = make_order(ORDER_ALLOW_DENY);
    EXPECT_EQ(exec_access_control(nullptr, order), LSI_OK);
    free_dir_list(order);
}

TEST_F(ExecAclTest, NullDirectivesReturnsOk)
{
    EXPECT_EQ(exec_access_control(session_.handle(), nullptr), LSI_OK);
}

TEST_F(ExecAclTest, NoAclDirectivesReturnsOk)
{
    /* Directive list with no ACL-related directives */
    session_.set_client_ip("10.0.0.1");
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_HEADER_SET;
    d->name = strdup("X-Test");
    d->value = strdup("val");
    d->next = nullptr;

    EXPECT_EQ(exec_access_control(session_.handle(), d), LSI_OK);

    free_dir_list(d);
}

TEST_F(ExecAclTest, MultipleAllowRules)
{
    /* Multiple Allow rules, one matches */
    session_.set_client_ip("10.0.0.1");
    auto *order = make_order(ORDER_ALLOW_DENY);
    auto *a1 = make_allow("192.168.0.0/16");
    auto *a2 = make_allow("10.0.0.0/8");
    order->next = a1;
    a1->next = a2;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_OK);

    free_dir_list(order);
}

TEST_F(ExecAclTest, MultipleDenyRules)
{
    /* Multiple Deny rules, one matches */
    session_.set_client_ip("10.0.0.1");
    auto *order = make_order(ORDER_DENY_ALLOW);
    auto *d1 = make_deny("192.168.0.0/16");
    auto *d2 = make_deny("10.0.0.0/8");
    order->next = d1;
    d1->next = d2;

    EXPECT_EQ(exec_access_control(session_.handle(), order), LSI_ERROR);
    EXPECT_EQ(session_.get_status_code(), 403);

    free_dir_list(order);
}
