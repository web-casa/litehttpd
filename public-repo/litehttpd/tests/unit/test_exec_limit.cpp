/**
 * test_exec_limit.cpp - Unit tests for Limit/LimitExcept executor
 *
 * Tests Limit (method in list → exec) and LimitExcept (method not in list → exec),
 * multi-method lists, and parsing/printing round-trip.
 *
 * Validates: Requirements 9.1-9.7
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <string>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_limit.h"
#include "htaccess_directive.h"
#include "htaccess_parser.h"
#include "htaccess_printer.h"
}

static htaccess_directive_t *parse(const char *input) {
    return htaccess_parse(input, strlen(input), "test");
}

/* Limit: method in list → should exec */
TEST(LimitExecTest, LimitMethodInList)
{
    const char *input =
        "<Limit GET POST>\n"
        "Require all denied\n"
        "</Limit>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_LIMIT);

    EXPECT_EQ(limit_should_exec(dirs, "GET"), 1);
    EXPECT_EQ(limit_should_exec(dirs, "POST"), 1);
    htaccess_directives_free(dirs);
}

/* Limit: method not in list → should not exec */
TEST(LimitExecTest, LimitMethodNotInList)
{
    const char *input =
        "<Limit GET POST>\n"
        "Require all denied\n"
        "</Limit>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    EXPECT_EQ(limit_should_exec(dirs, "PUT"), 0);
    EXPECT_EQ(limit_should_exec(dirs, "DELETE"), 0);
    htaccess_directives_free(dirs);
}

/* LimitExcept: method not in list → should exec */
TEST(LimitExecTest, LimitExceptMethodNotInList)
{
    const char *input =
        "<LimitExcept GET POST>\n"
        "Require all denied\n"
        "</LimitExcept>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_LIMIT_EXCEPT);

    EXPECT_EQ(limit_should_exec(dirs, "PUT"), 1);
    EXPECT_EQ(limit_should_exec(dirs, "DELETE"), 1);
    htaccess_directives_free(dirs);
}

/* LimitExcept: method in list → should not exec */
TEST(LimitExecTest, LimitExceptMethodInList)
{
    const char *input =
        "<LimitExcept GET POST>\n"
        "Require all denied\n"
        "</LimitExcept>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    EXPECT_EQ(limit_should_exec(dirs, "GET"), 0);
    EXPECT_EQ(limit_should_exec(dirs, "POST"), 0);
    htaccess_directives_free(dirs);
}

/* Limit with children parsed correctly */
TEST(LimitExecTest, LimitChildrenParsed)
{
    const char *input =
        "<Limit POST PUT>\n"
        "Require all denied\n"
        "Require ip 10.0.0.0/8\n"
        "</Limit>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_LIMIT);

    int count = 0;
    for (auto *c = dirs->data.limit.children; c; c = c->next)
        count++;
    EXPECT_EQ(count, 2);

    htaccess_directives_free(dirs);
}

/* Round-trip: parse → print → parse */
TEST(LimitExecTest, RoundTrip)
{
    const char *input =
        "<Limit GET POST>\n"
        "Require all denied\n"
        "</Limit>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    char *printed = htaccess_print(dirs);
    ASSERT_NE(printed, nullptr);

    auto *dirs2 = parse(printed);
    ASSERT_NE(dirs2, nullptr);
    EXPECT_EQ(dirs2->type, DIR_LIMIT);
    EXPECT_STREQ(dirs2->data.limit.methods, dirs->data.limit.methods);

    htaccess_directives_free(dirs);
    htaccess_directives_free(dirs2);
    free(printed);
}

/* Case-insensitive method matching */
TEST(LimitExecTest, CaseInsensitiveMethodMatch)
{
    const char *input =
        "<Limit GET>\n"
        "Require all denied\n"
        "</Limit>\n";
    auto *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    EXPECT_EQ(limit_should_exec(dirs, "get"), 1);
    EXPECT_EQ(limit_should_exec(dirs, "Get"), 1);
    htaccess_directives_free(dirs);
}
