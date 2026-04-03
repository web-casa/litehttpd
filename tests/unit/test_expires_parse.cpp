/**
 * test_expires_parse.cpp - Unit tests for Expires duration parsing
 *
 * Validates: Requirements 10.4
 */
#include <gtest/gtest.h>

extern "C" {
#include "htaccess_expires.h"
}

/* ==================================================================
 *  Individual time units (singular)
 * ================================================================== */

TEST(ExpiresParse, Seconds_Singular)
{
    EXPECT_EQ(parse_expires_duration("access plus 1 second"), 1L);
}

TEST(ExpiresParse, Minutes_Singular)
{
    EXPECT_EQ(parse_expires_duration("access plus 1 minute"), 60L);
}

TEST(ExpiresParse, Hours_Singular)
{
    EXPECT_EQ(parse_expires_duration("access plus 1 hour"), 3600L);
}

TEST(ExpiresParse, Days_Singular)
{
    EXPECT_EQ(parse_expires_duration("access plus 1 day"), 86400L);
}

TEST(ExpiresParse, Months_Singular)
{
    EXPECT_EQ(parse_expires_duration("access plus 1 month"), 2592000L);
}

TEST(ExpiresParse, Years_Singular)
{
    EXPECT_EQ(parse_expires_duration("access plus 1 year"), 31536000L);
}

/* ==================================================================
 *  Individual time units (plural)
 * ================================================================== */

TEST(ExpiresParse, Seconds_Plural)
{
    EXPECT_EQ(parse_expires_duration("access plus 5 seconds"), 5L);
}

TEST(ExpiresParse, Minutes_Plural)
{
    EXPECT_EQ(parse_expires_duration("access plus 3 minutes"), 180L);
}

TEST(ExpiresParse, Hours_Plural)
{
    EXPECT_EQ(parse_expires_duration("access plus 2 hours"), 7200L);
}

TEST(ExpiresParse, Days_Plural)
{
    EXPECT_EQ(parse_expires_duration("access plus 7 days"), 604800L);
}

TEST(ExpiresParse, Months_Plural)
{
    EXPECT_EQ(parse_expires_duration("access plus 6 months"), 15552000L);
}

TEST(ExpiresParse, Years_Plural)
{
    EXPECT_EQ(parse_expires_duration("access plus 2 years"), 63072000L);
}

/* ==================================================================
 *  Combined formats
 * ================================================================== */

TEST(ExpiresParse, Combined_MonthAndDays)
{
    /* 1 month (2592000) + 2 days (172800) = 2764800 */
    EXPECT_EQ(parse_expires_duration("access plus 1 month 2 days"), 2764800L);
}

TEST(ExpiresParse, Combined_YearMonthDay)
{
    /* 1 year (31536000) + 6 months (15552000) + 15 days (1296000) = 48384000 */
    EXPECT_EQ(parse_expires_duration("access plus 1 year 6 months 15 days"),
              48384000L);
}

TEST(ExpiresParse, Combined_HoursMinutesSeconds)
{
    /* 2 hours (7200) + 30 minutes (1800) + 45 seconds (45) = 9045 */
    EXPECT_EQ(parse_expires_duration("access plus 2 hours 30 minutes 45 seconds"),
              9045L);
}

/* ==================================================================
 *  Zero values
 * ================================================================== */

TEST(ExpiresParse, ZeroSeconds)
{
    EXPECT_EQ(parse_expires_duration("access plus 0 seconds"), 0L);
}

/* ==================================================================
 *  Case insensitivity
 * ================================================================== */

TEST(ExpiresParse, CaseInsensitive_AllUpper)
{
    EXPECT_EQ(parse_expires_duration("ACCESS PLUS 1 HOUR"), 3600L);
}

TEST(ExpiresParse, CaseInsensitive_MixedCase)
{
    EXPECT_EQ(parse_expires_duration("Access Plus 1 Month"), 2592000L);
}

TEST(ExpiresParse, CaseInsensitive_UnitPlural)
{
    EXPECT_EQ(parse_expires_duration("access plus 3 DAYS"), 259200L);
}

/* ==================================================================
 *  Invalid formats
 * ================================================================== */

TEST(ExpiresParse, NullInput_ReturnsError)
{
    EXPECT_EQ(parse_expires_duration(nullptr), -1L);
}

TEST(ExpiresParse, EmptyString_ReturnsError)
{
    EXPECT_EQ(parse_expires_duration(""), -1L);
}

TEST(ExpiresParse, MissingAccess_ReturnsError)
{
    EXPECT_EQ(parse_expires_duration("plus 1 hour"), -1L);
}

TEST(ExpiresParse, MissingPlus_ReturnsError)
{
    EXPECT_EQ(parse_expires_duration("access 1 hour"), -1L);
}

TEST(ExpiresParse, MissingNumber_ReturnsError)
{
    EXPECT_EQ(parse_expires_duration("access plus hours"), -1L);
}

TEST(ExpiresParse, MissingUnit_ReturnsError)
{
    EXPECT_EQ(parse_expires_duration("access plus 1"), -1L);
}

TEST(ExpiresParse, UnknownUnit_ReturnsError)
{
    EXPECT_EQ(parse_expires_duration("access plus 1 fortnight"), -1L);
}

TEST(ExpiresParse, NoPairs_ReturnsError)
{
    EXPECT_EQ(parse_expires_duration("access plus"), -1L);
}

/* ==================================================================
 *  Compact format: A<seconds> / M<seconds>
 * ================================================================== */

TEST(ExpiresParse, CompactAccess_OneYear)
{
    /* WordPress common: A31536000 = 1 year */
    EXPECT_EQ(parse_expires_duration("A31536000"), 31536000L);
}

TEST(ExpiresParse, CompactAccess_OneMonth)
{
    /* A2592000 = 30 days */
    EXPECT_EQ(parse_expires_duration("A2592000"), 2592000L);
}

TEST(ExpiresParse, CompactModification_OneYear)
{
    /* M treated as access time (LSIAPI limitation) */
    EXPECT_EQ(parse_expires_duration("M31536000"), 31536000L);
}

TEST(ExpiresParse, CompactAccess_Zero)
{
    EXPECT_EQ(parse_expires_duration("A0"), 0L);
}

TEST(ExpiresParse, CompactAccess_Lowercase)
{
    EXPECT_EQ(parse_expires_duration("a3600"), 3600L);
}

TEST(ExpiresParse, CompactModification_Lowercase)
{
    EXPECT_EQ(parse_expires_duration("m86400"), 86400L);
}

TEST(ExpiresParse, CompactAccess_WithLeadingSpace)
{
    EXPECT_EQ(parse_expires_duration("  A3600"), 3600L);
}

TEST(ExpiresParse, CompactInvalid_NoDigits)
{
    /* "Axyz" should not match compact format; falls through to verbose */
    EXPECT_EQ(parse_expires_duration("Axyz"), -1L);
}

TEST(ExpiresParse, CompactInvalid_JustLetter)
{
    EXPECT_EQ(parse_expires_duration("A"), -1L);
}
