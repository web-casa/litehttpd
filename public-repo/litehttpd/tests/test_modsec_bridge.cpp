/**
 * test_modsec_bridge.cpp — unit tests for the SecRuleEngine → OLS
 * modsecurity-ls bridge.
 *
 * The contract under test (mirrors isBypassCheck() in OLS
 * src/modules/modsecurity-ls/mod_security.cpp):
 *   .htaccess "SecRuleEngine Off"  → request env "modsecurity" == "off"
 *   "SecRuleEngine On"             → env untouched
 *   "SecRuleEngine DetectionOnly"  → env untouched (logged, treated as On)
 *
 * The mock session records set_env calls, so these tests pin both the
 * variable name and the exact value the daemon-side module compares with
 * strncasecmp(var, "off", 3).
 */
#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_directive.h"
#include "htaccess_exec_modsec.h"
}

/* Free a whole list the way the executors assume ownership of it. */
static void free_list(htaccess_directive_t *head)
{
    while (head) {
        htaccess_directive_t *next = head->next;
        free(head->value);
        free(head->name);
        free(head);
        head = next;
    }
}

namespace {

htaccess_directive_t *make_sec_rule_engine(const char *value)
{
    auto *d = static_cast<htaccess_directive_t *>(calloc(1, sizeof(htaccess_directive_t)));
    d->type = DIR_SEC_RULE_ENGINE;
    d->value = strdup(value);
    d->line_number = 1;
    return d;
}

htaccess_directive_t *make_other(directive_type_t type)
{
    auto *d = static_cast<htaccess_directive_t *>(calloc(1, sizeof(htaccess_directive_t)));
    d->type = type;
    d->value = strdup("whatever");
    d->line_number = 1;
    return d;
}

class ModSecBridgeTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }

    MockSession session_;
};

} // namespace

TEST_F(ModSecBridgeTest, OffSetsBypassEnvExactly)
{
    htaccess_directive_t *d = make_sec_rule_engine("off");
    ASSERT_EQ(exec_modsec_bridge(session_.handle(), d), LSI_OK);
    free_list(d);

    /* Variable name AND value must match the modsecurity-ls contract
     * byte-for-byte: get_req_env("modsecurity") == "off" (3 bytes). */
    EXPECT_TRUE(session_.has_env_var("modsecurity"));
    EXPECT_EQ(session_.get_env_var("modsecurity"), std::string("off"));
}

TEST_F(ModSecBridgeTest, OnLeavesEnvUntouched)
{
    htaccess_directive_t *d = make_sec_rule_engine("on");
    ASSERT_EQ(exec_modsec_bridge(session_.handle(), d), LSI_OK);
    free_list(d);

    /* Absent env means normal processing for modsecurity-ls; setting
     * anything here could clobber the cache module's own bypass. */
    EXPECT_FALSE(session_.has_env_var("modsecurity"));
}

TEST_F(ModSecBridgeTest, DetectionOnlyLeavesEnvUntouched)
{
    /* Cannot be honoured per-request through libmodsecurity; the bridge
     * must fail toward protection (env untouched), never bypass. */
    htaccess_directive_t *d = make_sec_rule_engine("detectiononly");
    ASSERT_EQ(exec_modsec_bridge(session_.handle(), d), LSI_OK);
    free_list(d);

    EXPECT_FALSE(session_.has_env_var("modsecurity"));
}

TEST_F(ModSecBridgeTest, LastDirectiveWins_ApacheMergeOrder)
{
    /* Deeper .htaccess overrides shallower: after merge the list is
     * root-first, so the LAST SecRuleEngine in the merged list is the
     * most-specific one and must win. */
    htaccess_directive_t *root = make_sec_rule_engine("off");
    htaccess_directive_t *leaf = make_sec_rule_engine("on");
    root->next = leaf;

    ASSERT_EQ(exec_modsec_bridge(session_.handle(), root), LSI_OK);
    free_list(root);

    EXPECT_FALSE(session_.has_env_var("modsecurity"));
}

TEST_F(ModSecBridgeTest, LastDirectiveWins_OffAfterOn)
{
    htaccess_directive_t *root = make_sec_rule_engine("on");
    htaccess_directive_t *leaf = make_sec_rule_engine("off");
    root->next = leaf;

    ASSERT_EQ(exec_modsec_bridge(session_.handle(), root), LSI_OK);
    free_list(root);

    EXPECT_TRUE(session_.has_env_var("modsecurity"));
    EXPECT_EQ(session_.get_env_var("modsecurity"), std::string("off"));
}

TEST_F(ModSecBridgeTest, IgnoresOtherDirectives)
{
    htaccess_directive_t *list = make_other(DIR_HEADER_SET);
    htaccess_directive_t *sec = make_sec_rule_engine("off");
    list->next = sec;

    ASSERT_EQ(exec_modsec_bridge(session_.handle(), list), LSI_OK);
    free_list(list);

    EXPECT_TRUE(session_.has_env_var("modsecurity"));
}

TEST_F(ModSecBridgeTest, NullArgumentsAreSafe)
{
    EXPECT_EQ(exec_modsec_bridge(nullptr, nullptr), LSI_OK);
    EXPECT_EQ(exec_modsec_bridge(session_.handle(), nullptr), LSI_OK);

    htaccess_directive_t *d = make_sec_rule_engine("off");
    EXPECT_EQ(exec_modsec_bridge(nullptr, d), LSI_OK);
    free_list(d);
}
