/**
 * prop_require.cpp - Property-based tests for Require access control
 *
 * Feature: htaccess-v2-enhancements, Properties 33-35
 *
 * Property 33: Require ip/all access control evaluation
 * Property 34: RequireAny OR / RequireAll AND logic
 * Property 35: Require takes precedence over Order/Allow/Deny
 *
 * Validates: Requirements 8.1-8.7
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <cstring>
#include <cstdlib>
#include <sstream>

#include "mock_lsiapi.h"
#include "gen_cidr.h"

extern "C" {
#include "htaccess_exec_require.h"
#include "htaccess_directive.h"
#include "htaccess_cidr.h"
}

/* ---- Helpers ---- */

static htaccess_directive_t *make_dir(directive_type_t type, const char *val = nullptr)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = type;
    if (val) d->value = strdup(val);
    return d;
}

static void free_dirs(htaccess_directive_t *head)
{
    while (head) {
        auto *n = head->next;
        free(head->name);
        free(head->value);
        /* Free container children */
        if (head->type == DIR_REQUIRE_ANY_OPEN || head->type == DIR_REQUIRE_ALL_OPEN)
            free_dirs(head->data.require_container.children);
        free(head);
        head = n;
    }
}

static std::string ip_to_string(uint32_t ip)
{
    return std::to_string((ip >> 24) & 0xFF) + "." +
           std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 8) & 0xFF) + "." +
           std::to_string(ip & 0xFF);
}

/* Generate a random IPv4 address as uint32_t */
static rc::Gen<uint32_t> genIPv4()
{
    return rc::gen::arbitrary<uint32_t>();
}

/* Generate a CIDR string and its parsed form */
struct CidrInfo {
    std::string str;
    uint32_t network;
    uint32_t mask;
};

static rc::Gen<CidrInfo> genCidr()
{
    return rc::gen::map(
        rc::gen::pair(rc::gen::arbitrary<uint32_t>(), rc::gen::inRange(8, 33)),
        [](const std::pair<uint32_t, int> &p) -> CidrInfo {
            uint32_t ip = p.first;
            int prefix = p.second;
            uint32_t mask = (prefix == 0) ? 0 : (~0U << (32 - prefix));
            uint32_t network = ip & mask;
            std::string s = ip_to_string(network) + "/" + std::to_string(prefix);
            return {s, network, mask};
        });
}

/* ---- Fixture ---- */

class RequirePropFixture : public ::testing::Test {
public:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
protected:
    MockSession session_;
};

/* Property 33a: Require all granted allows any IP */
RC_GTEST_FIXTURE_PROP(RequirePropFixture, AllGrantedAllowsAnyIP, ())
{
    auto ip = *genIPv4();
    std::string ip_str = ip_to_string(ip);

    auto *d = make_dir(DIR_REQUIRE_ALL_GRANTED);
    int rc = exec_require(session_.handle(), d, ip_str.c_str(), 0);
    RC_ASSERT(rc == LSI_OK);
    free_dirs(d);
}

/* Property 33b: Require all denied denies any IP */
RC_GTEST_FIXTURE_PROP(RequirePropFixture, AllDeniedDeniesAnyIP, ())
{
    auto ip = *genIPv4();
    std::string ip_str = ip_to_string(ip);

    auto *d = make_dir(DIR_REQUIRE_ALL_DENIED);
    int rc = exec_require(session_.handle(), d, ip_str.c_str(), 0);
    RC_ASSERT(rc == LSI_ERROR);
    free_dirs(d);
}

/* Property 33c: Require ip grants iff IP in CIDR */
RC_GTEST_FIXTURE_PROP(RequirePropFixture, RequireIpGrantsIffInCidr, ())
{
    auto cidr = *genCidr();
    auto ip = *genIPv4();
    std::string ip_str = ip_to_string(ip);

    auto *d = make_dir(DIR_REQUIRE_IP, cidr.str.c_str());
    int rc = exec_require(session_.handle(), d, ip_str.c_str(), 0);

    bool in_range = ((ip & cidr.mask) == cidr.network);
    if (in_range)
        RC_ASSERT(rc == LSI_OK);
    else
        RC_ASSERT(rc == LSI_ERROR);

    free_dirs(d);
}

/* Property 33d: Require not ip denies iff IP in CIDR */
RC_GTEST_FIXTURE_PROP(RequirePropFixture, RequireNotIpDeniesIffInCidr, ())
{
    auto cidr = *genCidr();
    auto ip = *genIPv4();
    std::string ip_str = ip_to_string(ip);

    auto *d = make_dir(DIR_REQUIRE_NOT_IP, cidr.str.c_str());
    int rc = exec_require(session_.handle(), d, ip_str.c_str(), 0);

    bool in_range = ((ip & cidr.mask) == cidr.network);
    if (in_range)
        RC_ASSERT(rc == LSI_ERROR);
    else
        RC_ASSERT(rc == LSI_OK);

    free_dirs(d);
}

/* Property 34a: RequireAny grants if at least one child grants */
RC_GTEST_FIXTURE_PROP(RequirePropFixture, RequireAnyOrLogic, ())
{
    auto cidr1 = *genCidr();
    auto cidr2 = *genCidr();
    auto ip = *genIPv4();
    std::string ip_str = ip_to_string(ip);

    /* Build RequireAny container with two Require ip children */
    auto *child1 = make_dir(DIR_REQUIRE_IP, cidr1.str.c_str());
    auto *child2 = make_dir(DIR_REQUIRE_IP, cidr2.str.c_str());
    child1->next = child2;

    auto *container = make_dir(DIR_REQUIRE_ANY_OPEN);
    container->data.require_container.children = child1;

    int rc = exec_require(session_.handle(), container, ip_str.c_str(), 0);

    bool match1 = ((ip & cidr1.mask) == cidr1.network);
    bool match2 = ((ip & cidr2.mask) == cidr2.network);
    bool expected_grant = match1 || match2;

    if (expected_grant)
        RC_ASSERT(rc == LSI_OK);
    else
        RC_ASSERT(rc == LSI_ERROR);

    /* Don't use free_dirs — container owns children */
    free(container->data.require_container.children->next->value);
    free(container->data.require_container.children->next);
    free(container->data.require_container.children->value);
    free(container->data.require_container.children);
    free(container);
}

/* Property 34b: RequireAll grants only if all children grant */
RC_GTEST_FIXTURE_PROP(RequirePropFixture, RequireAllAndLogic, ())
{
    auto cidr1 = *genCidr();
    auto cidr2 = *genCidr();
    auto ip = *genIPv4();
    std::string ip_str = ip_to_string(ip);

    auto *child1 = make_dir(DIR_REQUIRE_IP, cidr1.str.c_str());
    auto *child2 = make_dir(DIR_REQUIRE_IP, cidr2.str.c_str());
    child1->next = child2;

    auto *container = make_dir(DIR_REQUIRE_ALL_OPEN);
    container->data.require_container.children = child1;

    int rc = exec_require(session_.handle(), container, ip_str.c_str(), 0);

    bool match1 = ((ip & cidr1.mask) == cidr1.network);
    bool match2 = ((ip & cidr2.mask) == cidr2.network);
    bool expected_grant = match1 && match2;

    if (expected_grant)
        RC_ASSERT(rc == LSI_OK);
    else
        RC_ASSERT(rc == LSI_ERROR);

    free(container->data.require_container.children->next->value);
    free(container->data.require_container.children->next);
    free(container->data.require_container.children->value);
    free(container->data.require_container.children);
    free(container);
}

/* Property 35: Require takes precedence over Order/Allow/Deny */
RC_GTEST_FIXTURE_PROP(RequirePropFixture, RequirePrecedenceOverOrderAllowDeny, ())
{
    auto ip = *genIPv4();
    std::string ip_str = ip_to_string(ip);

    /* Order Deny,Allow + Deny from all + Require all granted */
    auto *order = make_dir(DIR_ORDER);
    order->data.acl.order = ORDER_DENY_ALLOW;
    auto *deny = make_dir(DIR_DENY_FROM, "all");
    auto *req = make_dir(DIR_REQUIRE_ALL_GRANTED);
    order->next = deny;
    deny->next = req;

    int rc = exec_require(session_.handle(), order, ip_str.c_str(), 0);
    /* Require all granted should override Deny from all */
    RC_ASSERT(rc == LSI_OK);

    free(deny->value);
    free(order);
    free(deny);
    free(req);
}
