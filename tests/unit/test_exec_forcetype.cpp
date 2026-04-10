/**
 * test_exec_forcetype.cpp - Unit tests for ForceType executor
 */
#include <gtest/gtest.h>
#include <cstring>
#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_forcetype.h"
#include "htaccess_parser.h"
#include "htaccess_directive.h"
}

class ForceTypeTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
    MockSession session_;
};

TEST_F(ForceTypeTest, ParseForceType) {
    const char *input = "ForceType application/json\n";
    auto *d = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_FORCE_TYPE);
    EXPECT_STREQ(d->value, "application/json");
    htaccess_directives_free(d);
}

TEST_F(ForceTypeTest, SetsContentType) {
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_FORCE_TYPE;
    d->value = strdup("application/pdf");

    EXPECT_EQ(exec_force_type(session_.handle(), d), LSI_OK);
    EXPECT_EQ(session_.get_response_header("Content-Type"), "application/pdf");
    htaccess_directives_free(d);
}

TEST_F(ForceTypeTest, OverridesPreviousContentType) {
    session_.add_response_header("Content-Type", "text/html");
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_FORCE_TYPE;
    d->value = strdup("application/json");

    EXPECT_EQ(exec_force_type(session_.handle(), d), LSI_OK);
    EXPECT_EQ(session_.get_response_header("Content-Type"), "application/json");
    htaccess_directives_free(d);
}
