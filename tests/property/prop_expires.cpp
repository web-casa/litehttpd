/**
 * prop_expires.cpp - Property-based test for Expires duration parsing
 *
 * Feature: ols-htaccess-module, Property 20: Expires 时长解析
 *
 * Validates: Requirements 10.4
 *
 * Property: For any valid expiration duration format string
 * ("access plus N unit [N unit ...]"), parse_expires_duration returns
 * the correct total seconds value.
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <vector>
#include <sstream>

extern "C" {
#include "htaccess_expires.h"
}

/* Time unit multipliers — must match the implementation */
static const struct {
    const char *singular;
    const char *plural;
    long        multiplier;
} kUnits[] = {
    { "second",  "seconds",  1L        },
    { "minute",  "minutes",  60L       },
    { "hour",    "hours",    3600L     },
    { "day",     "days",     86400L    },
    { "month",   "months",   2592000L  },  /* 30 days */
    { "year",    "years",    31536000L },  /* 365 days */
};

static constexpr int kNumUnits = 6;

/**
 * Helper: apply random casing to a string.
 * Each character is independently upper- or lower-cased.
 */
static rc::Gen<std::string> genRandomCase(const std::string &base)
{
    return rc::gen::map(
        rc::gen::container<std::vector<bool>>(base.size(), rc::gen::arbitrary<bool>()),
        [base](const std::vector<bool> &upper) {
            std::string out = base;
            for (size_t i = 0; i < out.size(); i++) {
                out[i] = upper[i] ? toupper(out[i]) : tolower(out[i]);
            }
            return out;
        });
}

/**
 * Property 20: Expires 时长解析
 *
 * Generate 1-4 random (quantity, unit_index) pairs, build a valid
 * "access plus ..." string, and verify parse_expires_duration returns
 * the expected total seconds.
 */
RC_GTEST_PROP(ExpiresProperty, DurationParsingReturnsCorrectSeconds, ())
{
    /* Number of time-unit pairs: 1 to 4 */
    auto numPairs = *rc::gen::inRange(1, 5);

    std::ostringstream ss;
    long expectedTotal = 0;

    /* Build "access plus" prefix with random casing */
    auto accessWord = *genRandomCase("access");
    auto plusWord    = *genRandomCase("plus");
    ss << accessWord << " " << plusWord;

    for (int i = 0; i < numPairs; i++) {
        /* Random quantity in [0, 500] */
        auto qty = *rc::gen::inRange(0, 501);

        /* Random unit index */
        auto unitIdx = *rc::gen::inRange(0, kNumUnits);

        /* Randomly choose singular or plural form */
        auto usePlural = *rc::gen::arbitrary<bool>();
        std::string unitStr = usePlural
            ? kUnits[unitIdx].plural
            : kUnits[unitIdx].singular;

        /* Apply random casing to the unit */
        auto casedUnit = *genRandomCase(unitStr);

        ss << " " << qty << " " << casedUnit;
        expectedTotal += (long)qty * kUnits[unitIdx].multiplier;
    }

    std::string input = ss.str();
    long result = parse_expires_duration(input.c_str());

    RC_ASSERT(result == expectedTotal);
}
