/**
 * prop_acl.cpp - Property-based tests for access control executor
 *
 * Feature: ols-htaccess-module
 *
 * Property 12: ACL 评估正确性
 *
 * Generates random Order type, Allow/Deny rule sets, and client IP.
 * Verifies evaluation result follows Apache ACL semantics.
 *
 * **Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.6**
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>

#include "mock_lsiapi.h"
#include "gen_cidr.h"

extern "C" {
#include "htaccess_exec_acl.h"
#include "htaccess_directive.h"
#include "htaccess_cidr.h"
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_dir(directive_type_t type,
                                      const char *name,
                                      const char *value)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = type;
    d->line_number = 1;
    d->name = name ? strdup(name) : nullptr;
    d->value = value ? strdup(value) : nullptr;
    d->next = nullptr;
    return d;
}

static htaccess_directive_t *make_order_dir(acl_order_t order)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ORDER;
    d->line_number = 1;
    d->name = nullptr;
    d->value = nullptr;
    d->data.acl.order = order;
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
 * Convert uint32_t IP (host byte order) to dotted-decimal string.
 */
static std::string ip_to_string(uint32_t ip)
{
    return std::to_string((ip >> 24) & 0xFF) + "." +
           std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 8) & 0xFF) + "." +
           std::to_string(ip & 0xFF);
}

/**
 * Reference implementation of Apache ACL semantics for verification.
 *
 * Returns true if access is ALLOWED, false if DENIED.
 */
static bool reference_acl_eval(acl_order_t order,
                               const std::vector<std::string> &allow_rules,
                               const std::vector<std::string> &deny_rules,
                               uint32_t client_ip)
{
    /* Check if client IP matches any rule in a rule set */
    auto matches_any = [&](const std::vector<std::string> &rules) -> bool {
        for (const auto &rule : rules) {
            if (strcasecmp(rule.c_str(), "all") == 0)
                return true;
            cidr_v4_t cidr;
            if (cidr_parse(rule.c_str(), &cidr) == 0) {
                if (cidr_match(&cidr, client_ip))
                    return true;
            }
        }
        return false;
    };

    bool allow_matched = matches_any(allow_rules);
    bool deny_matched = matches_any(deny_rules);

    if (order == ORDER_ALLOW_DENY) {
        /* Default DENY. Allowed only if Allow matches AND Deny does not. */
        return allow_matched && !deny_matched;
    } else {
        /* Default ALLOW. Denied only if Deny matches AND Allow does not. */
        if (deny_matched && !allow_matched)
            return false;
        return true;
    }
}

/* ------------------------------------------------------------------ */
/*  Generators                                                         */
/* ------------------------------------------------------------------ */

static rc::Gen<acl_order_t> genOrder()
{
    return rc::gen::element(ORDER_ALLOW_DENY, ORDER_DENY_ALLOW);
}

/**
 * Generate a list of ACL rule strings (CIDR or "all").
 * Keep the list small to avoid combinatorial explosion.
 */
static rc::Gen<std::vector<std::string>> genRuleSet()
{
    return rc::gen::mapcat(
        rc::gen::inRange(0, 4),
        [](int count) {
            return rc::gen::container<std::vector<std::string>>(
                static_cast<std::size_t>(count),
                gen::cidrOrAll()
            );
        });
}

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class AclPropertyFixture : public ::testing::Test {
public:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }

protected:
    MockSession session_;
};

/* ------------------------------------------------------------------ */
/*  Property 12: ACL 评估正确性                                        */
/*                                                                     */
/*  For any Order type, Allow/Deny rule sets, and client IP,           */
/*  the access control evaluation matches Apache ACL semantics.        */
/*                                                                     */
/*  **Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.6**              */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(AclPropertyFixture,
                      AclEvaluationFollowsApacheSemantics,
                      ())
{
    auto order = *genOrder();
    auto allow_rules = *genRuleSet();
    auto deny_rules = *genRuleSet();
    auto client_ip = *gen::ipv4Address();

    std::string ip_str = ip_to_string(client_ip);

    /* Build directive list: Order → Allow rules → Deny rules */
    htaccess_directive_t *head = make_order_dir(order);
    htaccess_directive_t *tail = head;

    for (const auto &rule : allow_rules) {
        auto *d = make_dir(DIR_ALLOW_FROM, nullptr, rule.c_str());
        tail->next = d;
        tail = d;
    }

    for (const auto &rule : deny_rules) {
        auto *d = make_dir(DIR_DENY_FROM, nullptr, rule.c_str());
        tail->next = d;
        tail = d;
    }

    /* Set up session with client IP */
    session_.set_client_ip(ip_str);
    session_.set_status_code(200);

    /* Execute */
    int result = exec_access_control(session_.handle(), head);

    /* Compute expected result */
    bool expected_allowed = reference_acl_eval(order, allow_rules, deny_rules,
                                               client_ip);

    if (expected_allowed) {
        RC_ASSERT(result == LSI_OK);
        RC_ASSERT(session_.get_status_code() == 200);
    } else {
        RC_ASSERT(result == LSI_ERROR);
        RC_ASSERT(session_.get_status_code() == 403);
    }

    free_dir_list(head);
}
