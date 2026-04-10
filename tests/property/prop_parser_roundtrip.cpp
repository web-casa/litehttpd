/**
 * prop_parser_roundtrip.cpp - Property-based test for parser/printer round-trip
 *
 * Feature: ols-htaccess-module, Property 1: .htaccess 解析/打印 Round-Trip
 *
 * Validates: Requirements 2.5, 2.6
 *
 * Property: For any valid .htaccess file content, parsing then printing then
 * parsing produces an equivalent set of Directive objects (same types, same
 * names, same values, same order).
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>

extern "C" {
#include "htaccess_directive.h"
#include "htaccess_parser.h"
#include "htaccess_printer.h"
}

/* ------------------------------------------------------------------ */
/*  Inline generators for simple alphanumeric strings                  */
/* ------------------------------------------------------------------ */

static const std::string kAlphaChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const std::string kAlnumChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const std::string kValueChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.";

/**
 * Generate a simple alphanumeric identifier (1-12 chars).
 * Starts with a letter to avoid parsing ambiguity.
 */
static rc::Gen<std::string> genAlphaIdent()
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, 12),
        [](int len) {
            return rc::gen::map(
                rc::gen::pair(
                    rc::gen::elementOf(kAlphaChars),
                    rc::gen::container<std::vector<char>>(
                        (std::size_t)(len - 1),
                        rc::gen::elementOf(kAlnumChars))),
                [](const std::pair<char, std::vector<char>> &p) {
                    return std::string(1, p.first) +
                           std::string(p.second.begin(), p.second.end());
                });
        });
}

/**
 * Generate a simple alphanumeric value (1-20 chars, no whitespace or quotes).
 */
static rc::Gen<std::string> genSimpleValue()
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, 21),
        [](int len) {
            return rc::gen::map(
                rc::gen::container<std::vector<char>>(
                    (std::size_t)len,
                    rc::gen::elementOf(kValueChars)),
                [](const std::vector<char> &v) {
                    return std::string(v.begin(), v.end());
                });
        });
}

/**
 * Generate a valid HTTP header name (e.g. X-Something).
 */
static rc::Gen<std::string> genHeaderName()
{
    auto prefixes = std::vector<std::string>{"X", "Content", "Cache", "Accept"};
    return rc::gen::map(
        rc::gen::pair(
            rc::gen::elementOf(prefixes),
            genAlphaIdent()),
        [](const std::pair<std::string, std::string> &p) {
            return p.first + "-" + p.second;
        });
}

/**
 * Generate a random CIDR or "all" for Allow/Deny from directives.
 */
static rc::Gen<std::string> genCidrOrAll()
{
    return rc::gen::oneOf(
        rc::gen::just(std::string("all")),
        rc::gen::map(
            rc::gen::tuple(
                rc::gen::inRange(0, 256),
                rc::gen::inRange(0, 256),
                rc::gen::inRange(0, 256),
                rc::gen::inRange(0, 256),
                rc::gen::inRange(8, 33)),
            [](const std::tuple<int,int,int,int,int> &t) {
                return std::to_string(std::get<0>(t)) + "." +
                       std::to_string(std::get<1>(t)) + "." +
                       std::to_string(std::get<2>(t)) + "." +
                       std::to_string(std::get<3>(t)) + "/" +
                       std::to_string(std::get<4>(t));
            }));
}

/* ------------------------------------------------------------------ */
/*  Directive line generators                                          */
/* ------------------------------------------------------------------ */

/**
 * Generate a single random .htaccess directive line from a subset of
 * simple types that are easy to generate and round-trip cleanly.
 */
static rc::Gen<std::string> genDirectiveLine()
{
    return rc::gen::oneOf(
        /* Header set <name> <value> */
        rc::gen::map(
            rc::gen::pair(genHeaderName(), genSimpleValue()),
            [](const std::pair<std::string, std::string> &p) {
                return "Header set " + p.first + " " + p.second;
            }),
        /* Header unset <name> */
        rc::gen::map(genHeaderName(),
            [](const std::string &n) {
                return "Header unset " + n;
            }),
        /* Header append <name> <value> */
        rc::gen::map(
            rc::gen::pair(genHeaderName(), genSimpleValue()),
            [](const std::pair<std::string, std::string> &p) {
                return "Header append " + p.first + " " + p.second;
            }),
        /* Header merge <name> <value> */
        rc::gen::map(
            rc::gen::pair(genHeaderName(), genSimpleValue()),
            [](const std::pair<std::string, std::string> &p) {
                return "Header merge " + p.first + " " + p.second;
            }),
        /* Header add <name> <value> */
        rc::gen::map(
            rc::gen::pair(genHeaderName(), genSimpleValue()),
            [](const std::pair<std::string, std::string> &p) {
                return "Header add " + p.first + " " + p.second;
            }),
        /* php_value <name> <value> */
        rc::gen::map(
            rc::gen::pair(genAlphaIdent(), genSimpleValue()),
            [](const std::pair<std::string, std::string> &p) {
                return "php_value " + p.first + " " + p.second;
            }),
        /* Order Allow,Deny / Deny,Allow */
        rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool allowDeny) -> std::string {
                return allowDeny ? "Order Allow,Deny" : "Order Deny,Allow";
            }),
        /* Allow from <cidr|all> */
        rc::gen::map(genCidrOrAll(),
            [](const std::string &v) {
                return "Allow from " + v;
            }),
        /* Deny from <cidr|all> */
        rc::gen::map(genCidrOrAll(),
            [](const std::string &v) {
                return "Deny from " + v;
            }),
        /* SetEnv <name> <value> */
        rc::gen::map(
            rc::gen::pair(genAlphaIdent(), genSimpleValue()),
            [](const std::pair<std::string, std::string> &p) {
                return "SetEnv " + p.first + " " + p.second;
            }),
        /* ExpiresActive On/Off */
        rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool on) -> std::string {
                return on ? "ExpiresActive On" : "ExpiresActive Off";
            }),
        /* BruteForceProtection On/Off */
        rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool on) -> std::string {
                return on ? "BruteForceProtection On" : "BruteForceProtection Off";
            })
    );
}

/* ------------------------------------------------------------------ */
/*  Directive comparison helpers                                       */
/* ------------------------------------------------------------------ */

/**
 * Compare two directive linked lists for structural equivalence.
 * Checks type, name, value, and type-specific fields.
 * Returns true if equivalent, false otherwise.
 */
static bool directives_equivalent(const htaccess_directive_t *a,
                                  const htaccess_directive_t *b)
{
    while (a && b) {
        if (a->type != b->type)
            return false;

        /* Compare name (both NULL or both equal) */
        if ((a->name == nullptr) != (b->name == nullptr))
            return false;
        if (a->name && b->name && strcmp(a->name, b->name) != 0)
            return false;

        /* Compare value (both NULL or both equal) */
        if ((a->value == nullptr) != (b->value == nullptr))
            return false;
        if (a->value && b->value && strcmp(a->value, b->value) != 0)
            return false;

        /* Compare type-specific fields */
        switch (a->type) {
        case DIR_ORDER:
            if (a->data.acl.order != b->data.acl.order)
                return false;
            break;
        case DIR_EXPIRES_ACTIVE:
            if (a->data.expires.active != b->data.expires.active)
                return false;
            break;
        case DIR_BRUTE_FORCE_PROTECTION:
            if (a->data.brute_force.enabled != b->data.brute_force.enabled)
                return false;
            break;
        case DIR_FILES_MATCH:
            /* Compare pattern */
            if ((a->data.files_match.pattern == nullptr) !=
                (b->data.files_match.pattern == nullptr))
                return false;
            if (a->data.files_match.pattern && b->data.files_match.pattern &&
                strcmp(a->data.files_match.pattern,
                       b->data.files_match.pattern) != 0)
                return false;
            /* Recursively compare children */
            if (!directives_equivalent(a->data.files_match.children,
                                       b->data.files_match.children))
                return false;
            break;
        default:
            break;
        }

        a = a->next;
        b = b->next;
    }

    /* Both should be exhausted */
    return (a == nullptr && b == nullptr);
}

/* ------------------------------------------------------------------ */
/*  Property test                                                      */
/* ------------------------------------------------------------------ */

/**
 * Property 1: .htaccess 解析/打印 Round-Trip
 *
 * Generate random valid .htaccess content (1-10 directives), then verify:
 *   parse(content) → print → parse produces equivalent directive lists.
 *
 * **Validates: Requirements 2.5, 2.6**
 */
RC_GTEST_PROP(ParserRoundTrip, ParsePrintParseProducesEquivalentDirectives, ())
{
    /* Generate 1-10 random directive lines */
    auto numDirectives = *rc::gen::inRange(1, 11);
    auto lines = *rc::gen::container<std::vector<std::string>>(
        (std::size_t)numDirectives, genDirectiveLine());

    /* Join into .htaccess content */
    std::string content;
    for (const auto &line : lines) {
        content += line + "\n";
    }

    /* Step 1: Parse the generated content */
    htaccess_directive_t *parsed1 = htaccess_parse(
        content.c_str(), content.size(), "roundtrip_test");

    /* Must produce at least one directive */
    RC_ASSERT(parsed1 != nullptr);

    /* Step 2: Print the parsed directives */
    char *printed = htaccess_print(parsed1);
    RC_ASSERT(printed != nullptr);

    /* Step 3: Re-parse the printed output */
    htaccess_directive_t *parsed2 = htaccess_parse(
        printed, strlen(printed), "roundtrip_test_reparse");
    RC_ASSERT(parsed2 != nullptr);

    /* Step 4: Compare the two directive lists for equivalence */
    RC_ASSERT(directives_equivalent(parsed1, parsed2));

    /* Cleanup */
    free(printed);
    htaccess_directives_free(parsed1);
    htaccess_directives_free(parsed2);
}
