/**
 * test_allow_override.cpp - Unit tests for AllowOverride category filtering
 *
 * Tests directive_category() mapping and the filter_by_allow_override()
 * behavior (indirectly through directive_category since the filter
 * function is internal to htaccess_dirwalker.c).
 */
#include <gtest/gtest.h>
#include <cstring>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_directive.h"
}

/* ================================================================== */
/*  directive_category() mapping tests                                 */
/* ================================================================== */

class AllowOverrideTest : public ::testing::Test {};

TEST_F(AllowOverrideTest, LimitCategory) {
    EXPECT_EQ(directive_category(DIR_ORDER), ALLOW_OVERRIDE_LIMIT);
    EXPECT_EQ(directive_category(DIR_ALLOW_FROM), ALLOW_OVERRIDE_LIMIT);
    EXPECT_EQ(directive_category(DIR_DENY_FROM), ALLOW_OVERRIDE_LIMIT);
    EXPECT_EQ(directive_category(DIR_LIMIT), ALLOW_OVERRIDE_LIMIT);
    EXPECT_EQ(directive_category(DIR_LIMIT_EXCEPT), ALLOW_OVERRIDE_LIMIT);
}

TEST_F(AllowOverrideTest, AuthCategory) {
    EXPECT_EQ(directive_category(DIR_AUTH_TYPE), ALLOW_OVERRIDE_AUTH);
    EXPECT_EQ(directive_category(DIR_AUTH_NAME), ALLOW_OVERRIDE_AUTH);
    EXPECT_EQ(directive_category(DIR_AUTH_USER_FILE), ALLOW_OVERRIDE_AUTH);
    EXPECT_EQ(directive_category(DIR_REQUIRE_VALID_USER), ALLOW_OVERRIDE_AUTH);
    EXPECT_EQ(directive_category(DIR_REQUIRE_ALL_GRANTED), ALLOW_OVERRIDE_AUTH);
    EXPECT_EQ(directive_category(DIR_REQUIRE_ALL_DENIED), ALLOW_OVERRIDE_AUTH);
    EXPECT_EQ(directive_category(DIR_REQUIRE_IP), ALLOW_OVERRIDE_AUTH);
    EXPECT_EQ(directive_category(DIR_REQUIRE_NOT_IP), ALLOW_OVERRIDE_AUTH);
    EXPECT_EQ(directive_category(DIR_REQUIRE_ANY_OPEN), ALLOW_OVERRIDE_AUTH);
    EXPECT_EQ(directive_category(DIR_REQUIRE_ALL_OPEN), ALLOW_OVERRIDE_AUTH);
    EXPECT_EQ(directive_category(DIR_SATISFY), ALLOW_OVERRIDE_AUTH);
}

TEST_F(AllowOverrideTest, FileInfoCategory) {
    EXPECT_EQ(directive_category(DIR_ADD_TYPE), ALLOW_OVERRIDE_FILEINFO);
    EXPECT_EQ(directive_category(DIR_ADD_HANDLER), ALLOW_OVERRIDE_FILEINFO);
    EXPECT_EQ(directive_category(DIR_SET_HANDLER), ALLOW_OVERRIDE_FILEINFO);
    EXPECT_EQ(directive_category(DIR_FORCE_TYPE), ALLOW_OVERRIDE_FILEINFO);
    EXPECT_EQ(directive_category(DIR_ADD_CHARSET), ALLOW_OVERRIDE_FILEINFO);
    EXPECT_EQ(directive_category(DIR_ADD_ENCODING), ALLOW_OVERRIDE_FILEINFO);
    EXPECT_EQ(directive_category(DIR_ADD_DEFAULT_CHARSET), ALLOW_OVERRIDE_FILEINFO);
    EXPECT_EQ(directive_category(DIR_DEFAULT_TYPE), ALLOW_OVERRIDE_FILEINFO);
    EXPECT_EQ(directive_category(DIR_DIRECTORY_INDEX), ALLOW_OVERRIDE_INDEXES);
    EXPECT_EQ(directive_category(DIR_REDIRECT), ALLOW_OVERRIDE_FILEINFO);
    EXPECT_EQ(directive_category(DIR_REDIRECT_MATCH), ALLOW_OVERRIDE_FILEINFO);
    EXPECT_EQ(directive_category(DIR_ERROR_DOCUMENT), ALLOW_OVERRIDE_FILEINFO);
    EXPECT_EQ(directive_category(DIR_REWRITE_ENGINE), ALLOW_OVERRIDE_FILEINFO);
    EXPECT_EQ(directive_category(DIR_REWRITE_BASE), ALLOW_OVERRIDE_FILEINFO);
    EXPECT_EQ(directive_category(DIR_REWRITE_COND), ALLOW_OVERRIDE_FILEINFO);
    EXPECT_EQ(directive_category(DIR_REWRITE_RULE), ALLOW_OVERRIDE_FILEINFO);
}

TEST_F(AllowOverrideTest, OptionsCategory) {
    /* DIR_OPTIONS has both Options + Indexes bits */
    int cat = directive_category(DIR_OPTIONS);
    EXPECT_TRUE(cat & ALLOW_OVERRIDE_OPTIONS);
    EXPECT_TRUE(cat & ALLOW_OVERRIDE_INDEXES);

    EXPECT_EQ(directive_category(DIR_EXPIRES_ACTIVE), ALLOW_OVERRIDE_OPTIONS);
    EXPECT_EQ(directive_category(DIR_EXPIRES_BY_TYPE), ALLOW_OVERRIDE_OPTIONS);
    EXPECT_EQ(directive_category(DIR_EXPIRES_DEFAULT), ALLOW_OVERRIDE_OPTIONS);
}

TEST_F(AllowOverrideTest, UnrestrictedDirectives) {
    /* Headers — always allowed */
    EXPECT_EQ(directive_category(DIR_HEADER_SET), ALLOW_OVERRIDE_ALL);
    EXPECT_EQ(directive_category(DIR_HEADER_UNSET), ALLOW_OVERRIDE_ALL);
    EXPECT_EQ(directive_category(DIR_HEADER_APPEND), ALLOW_OVERRIDE_ALL);

    /* PHP — always allowed */
    EXPECT_EQ(directive_category(DIR_PHP_VALUE), ALLOW_OVERRIDE_ALL);
    EXPECT_EQ(directive_category(DIR_PHP_FLAG), ALLOW_OVERRIDE_ALL);

    /* Environment — always allowed */
    EXPECT_EQ(directive_category(DIR_SETENV), ALLOW_OVERRIDE_ALL);
    EXPECT_EQ(directive_category(DIR_SETENVIF), ALLOW_OVERRIDE_ALL);

    /* Containers — always allowed */
    EXPECT_EQ(directive_category(DIR_IFMODULE), ALLOW_OVERRIDE_ALL);
    EXPECT_EQ(directive_category(DIR_FILES), ALLOW_OVERRIDE_ALL);
    EXPECT_EQ(directive_category(DIR_FILES_MATCH), ALLOW_OVERRIDE_ALL);

    /* Brute force — always allowed */
    EXPECT_EQ(directive_category(DIR_BRUTE_FORCE_PROTECTION), ALLOW_OVERRIDE_ALL);
}

/* ================================================================== */
/*  Bitmask logic tests                                                */
/* ================================================================== */

TEST_F(AllowOverrideTest, AllowOverrideNoneBlocksEverything) {
    int ao = ALLOW_OVERRIDE_NONE;

    /* Limit blocked */
    EXPECT_FALSE(ao & directive_category(DIR_ORDER));
    /* Auth blocked */
    EXPECT_FALSE(ao & directive_category(DIR_AUTH_TYPE));
    /* FileInfo blocked */
    EXPECT_FALSE(ao & directive_category(DIR_REDIRECT));
    /* Options blocked */
    EXPECT_FALSE(ao & directive_category(DIR_EXPIRES_ACTIVE));

    /* Module extension directives (cat=ALLOW_OVERRIDE_ALL) are ALWAYS allowed,
     * even under AllowOverride None. These are OLS-specific directives not
     * covered by Apache's AllowOverride categories. The filter special-cases
     * cat == ALLOW_OVERRIDE_ALL as always permitted. */
    EXPECT_EQ(directive_category(DIR_HEADER_SET), ALLOW_OVERRIDE_ALL);
    EXPECT_EQ(directive_category(DIR_PHP_VALUE), ALLOW_OVERRIDE_ALL);
    EXPECT_EQ(directive_category(DIR_SETENV), ALLOW_OVERRIDE_ALL);
}

TEST_F(AllowOverrideTest, AllowOverrideAllPermitsEverything) {
    int ao = ALLOW_OVERRIDE_ALL;

    EXPECT_TRUE(ao & directive_category(DIR_ORDER));
    EXPECT_TRUE(ao & directive_category(DIR_AUTH_TYPE));
    EXPECT_TRUE(ao & directive_category(DIR_REDIRECT));
    EXPECT_TRUE(ao & directive_category(DIR_EXPIRES_ACTIVE));
    EXPECT_TRUE(ao & directive_category(DIR_OPTIONS));
}

TEST_F(AllowOverrideTest, AllowOverrideLimitOnly) {
    int ao = ALLOW_OVERRIDE_LIMIT;

    /* Limit allowed */
    int cat = directive_category(DIR_ORDER);
    EXPECT_EQ(ao & cat, cat);

    /* Auth blocked */
    cat = directive_category(DIR_AUTH_TYPE);
    EXPECT_NE(ao & cat, cat);

    /* FileInfo blocked */
    cat = directive_category(DIR_REDIRECT);
    EXPECT_NE(ao & cat, cat);
}

TEST_F(AllowOverrideTest, AllowOverrideLimitAuth) {
    int ao = ALLOW_OVERRIDE_LIMIT | ALLOW_OVERRIDE_AUTH;

    /* Limit allowed */
    int cat = directive_category(DIR_DENY_FROM);
    EXPECT_EQ(ao & cat, cat);

    /* Auth allowed */
    cat = directive_category(DIR_REQUIRE_VALID_USER);
    EXPECT_EQ(ao & cat, cat);

    /* FileInfo blocked */
    cat = directive_category(DIR_ADD_TYPE);
    EXPECT_NE(ao & cat, cat);
}

TEST_F(AllowOverrideTest, OptionsWithoutIndexes) {
    /* AllowOverride Options (but not Indexes) */
    int ao = ALLOW_OVERRIDE_OPTIONS;

    /* Expires allowed (Options category) */
    int cat = directive_category(DIR_EXPIRES_ACTIVE);
    EXPECT_EQ(ao & cat, cat);

    /* DIR_OPTIONS needs both Options + Indexes bits */
    cat = directive_category(DIR_OPTIONS);
    /* Options bit is set but Indexes bit is not → partial match → should block */
    EXPECT_NE(ao & cat, cat);
}

TEST_F(AllowOverrideTest, OptionsWithIndexes) {
    int ao = ALLOW_OVERRIDE_OPTIONS | ALLOW_OVERRIDE_INDEXES;

    int cat = directive_category(DIR_OPTIONS);
    EXPECT_EQ(ao & cat, cat);
}
