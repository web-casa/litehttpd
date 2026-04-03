/**
 * gen_expires.h - Expires duration string generator for RapidCheck
 *
 * Generates random "access plus N unit" format strings for testing
 * the expires duration parser. Supports single and combined formats.
 *
 * Validates: Requirements 10.4
 */
#ifndef GEN_EXPIRES_H
#define GEN_EXPIRES_H

#include <rapidcheck.h>
#include <string>
#include <vector>
#include <utility>

namespace gen {

/**
 * Unit conversion factors (must match htaccess_expires.c).
 */
struct ExpiresUnit {
    const char *singular;
    const char *plural;
    long seconds;
};

static const ExpiresUnit kExpiresUnits[] = {
    {"second",  "seconds",  1L},
    {"minute",  "minutes",  60L},
    {"hour",    "hours",    3600L},
    {"day",     "days",     86400L},
    {"month",   "months",   2592000L},
    {"year",    "years",    31536000L},
};
static const int kNumExpiresUnits = 6;

/**
 * Result of generating an expires string: the string and expected seconds.
 */
using ExpiresResult = std::pair<std::string, long>;

/**
 * Generate a single "N unit" pair and its expected seconds value.
 */
inline rc::Gen<ExpiresResult> expiresComponent()
{
    return rc::gen::map(
        rc::gen::pair(
            rc::gen::inRange(1, 100),
            rc::gen::inRange(0, kNumExpiresUnits)),
        [](const std::pair<int, int> &p) -> ExpiresResult {
            int n = p.first;
            const auto &unit = kExpiresUnits[p.second];
            std::string unit_str = (n == 1) ? unit.singular : unit.plural;
            std::string component = std::to_string(n) + " " + unit_str;
            long secs = (long)n * unit.seconds;
            return {component, secs};
        });
}

/**
 * Generate a complete "access plus N unit [N unit ...]" string
 * with 1-3 components, along with the expected total seconds.
 */
inline rc::Gen<ExpiresResult> expiresDuration()
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, 4),
        [](int count) {
            return rc::gen::map(
                rc::gen::container<std::vector<ExpiresResult>>(
                    (std::size_t)count,
                    expiresComponent()),
                [](const std::vector<ExpiresResult> &components) -> ExpiresResult {
                    std::string result = "access plus";
                    long total_secs = 0;
                    for (const auto &c : components) {
                        result += " " + c.first;
                        total_secs += c.second;
                    }
                    return {result, total_secs};
                });
        });
}

/**
 * Generate just the expires duration string (without expected seconds).
 */
inline rc::Gen<std::string> expiresDurationString()
{
    return rc::gen::map(
        expiresDuration(),
        [](const ExpiresResult &r) { return r.first; });
}

} /* namespace gen */

#endif /* GEN_EXPIRES_H */
