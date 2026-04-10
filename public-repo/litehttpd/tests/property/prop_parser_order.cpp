/**
 * prop_parser_order.cpp - Property-based test for parser order preservation
 *
 * Feature: ols-htaccess-module, Property 2: 解析保序性
 *
 * Validates: Requirements 2.2, 9.3
 *
 * Property: For any .htaccess content containing N valid directives,
 * parsing produces exactly N Directive objects whose types match the
 * expected types from the generated lines, in the same order.
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <vector>
#include <cstring>

extern "C" {
#include "htaccess_directive.h"
#include "htaccess_parser.h"
}

/* ------------------------------------------------------------------ */
/*  Inline generators (reused from prop_parser_roundtrip.cpp)          */
/* ------------------------------------------------------------------ */

static const std::string kAlphaChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const std::string kAlnumChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const std::string kValueChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.";

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
/*  Tagged directive line generator                                    */
/*  Each generated line is paired with its expected directive_type_t.  */
/* ------------------------------------------------------------------ */

using TaggedLine = std::pair<std::string, directive_type_t>;

static rc::Gen<TaggedLine> genTaggedDirectiveLine()
{
    return rc::gen::oneOf(
        /* Header set <name> <value> → DIR_HEADER_SET */
        rc::gen::map(
            rc::gen::pair(genHeaderName(), genSimpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"Header set " + p.first + " " + p.second,
                        DIR_HEADER_SET};
            }),
        /* Header unset <name> → DIR_HEADER_UNSET */
        rc::gen::map(genHeaderName(),
            [](const std::string &n) -> TaggedLine {
                return {"Header unset " + n, DIR_HEADER_UNSET};
            }),
        /* Header append <name> <value> → DIR_HEADER_APPEND */
        rc::gen::map(
            rc::gen::pair(genHeaderName(), genSimpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"Header append " + p.first + " " + p.second,
                        DIR_HEADER_APPEND};
            }),
        /* Header merge <name> <value> → DIR_HEADER_MERGE */
        rc::gen::map(
            rc::gen::pair(genHeaderName(), genSimpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"Header merge " + p.first + " " + p.second,
                        DIR_HEADER_MERGE};
            }),
        /* Header add <name> <value> → DIR_HEADER_ADD */
        rc::gen::map(
            rc::gen::pair(genHeaderName(), genSimpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"Header add " + p.first + " " + p.second,
                        DIR_HEADER_ADD};
            }),
        /* php_value <name> <value> → DIR_PHP_VALUE */
        rc::gen::map(
            rc::gen::pair(genAlphaIdent(), genSimpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"php_value " + p.first + " " + p.second,
                        DIR_PHP_VALUE};
            }),
        /* Order Allow,Deny → DIR_ORDER */
        rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool allowDeny) -> TaggedLine {
                return {allowDeny ? "Order Allow,Deny" : "Order Deny,Allow",
                        DIR_ORDER};
            }),
        /* Allow from <cidr|all> → DIR_ALLOW_FROM */
        rc::gen::map(genCidrOrAll(),
            [](const std::string &v) -> TaggedLine {
                return {"Allow from " + v, DIR_ALLOW_FROM};
            }),
        /* Deny from <cidr|all> → DIR_DENY_FROM */
        rc::gen::map(genCidrOrAll(),
            [](const std::string &v) -> TaggedLine {
                return {"Deny from " + v, DIR_DENY_FROM};
            }),
        /* SetEnv <name> <value> → DIR_SETENV */
        rc::gen::map(
            rc::gen::pair(genAlphaIdent(), genSimpleValue()),
            [](const std::pair<std::string, std::string> &p) -> TaggedLine {
                return {"SetEnv " + p.first + " " + p.second, DIR_SETENV};
            }),
        /* ExpiresActive On/Off → DIR_EXPIRES_ACTIVE */
        rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool on) -> TaggedLine {
                return {on ? "ExpiresActive On" : "ExpiresActive Off",
                        DIR_EXPIRES_ACTIVE};
            }),
        /* BruteForceProtection On/Off → DIR_BRUTE_FORCE_PROTECTION */
        rc::gen::map(
            rc::gen::arbitrary<bool>(),
            [](bool on) -> TaggedLine {
                return {on ? "BruteForceProtection On"
                           : "BruteForceProtection Off",
                        DIR_BRUTE_FORCE_PROTECTION};
            })
    );
}

/* ------------------------------------------------------------------ */
/*  Helper: count directives in a linked list                          */
/* ------------------------------------------------------------------ */

static int count_directives(const htaccess_directive_t *head)
{
    int count = 0;
    for (const htaccess_directive_t *d = head; d; d = d->next)
        count++;
    return count;
}

/* ------------------------------------------------------------------ */
/*  Helper: collect directive types into a vector                      */
/* ------------------------------------------------------------------ */

static std::vector<directive_type_t>
collect_types(const htaccess_directive_t *head)
{
    std::vector<directive_type_t> types;
    for (const htaccess_directive_t *d = head; d; d = d->next)
        types.push_back(d->type);
    return types;
}

/* ------------------------------------------------------------------ */
/*  Property test                                                      */
/* ------------------------------------------------------------------ */

/**
 * Property 2: 解析保序性
 *
 * Generate N random valid directive lines (N in [1, 15]), join them with
 * newlines, parse with htaccess_parse(), then verify:
 *   1. The number of parsed directives equals N.
 *   2. The directive types match the expected types in the same order.
 *
 * Uses at least 100 iterations (configured via RC_PARAMS).
 *
 * **Validates: Requirements 2.2, 9.3**
 */
RC_GTEST_PROP(ParserOrder,
              ParsePreservesDirectiveCountAndTypeOrder,
              ())
{
    /* Generate N in [1, 15] tagged directive lines */
    auto numDirectives = *rc::gen::inRange(1, 16);
    auto taggedLines = *rc::gen::container<std::vector<TaggedLine>>(
        (std::size_t)numDirectives, genTaggedDirectiveLine());

    /* Build expected type sequence */
    std::vector<directive_type_t> expectedTypes;
    expectedTypes.reserve(taggedLines.size());
    for (const auto &tl : taggedLines)
        expectedTypes.push_back(tl.second);

    /* Join lines into .htaccess content */
    std::string content;
    for (const auto &tl : taggedLines)
        content += tl.first + "\n";

    /* Parse */
    htaccess_directive_t *parsed = htaccess_parse(
        content.c_str(), content.size(), "order_test");
    RC_ASSERT(parsed != nullptr);

    /* Verify count */
    int actualCount = count_directives(parsed);
    RC_ASSERT(actualCount == numDirectives);

    /* Verify type order */
    std::vector<directive_type_t> actualTypes = collect_types(parsed);
    RC_ASSERT(actualTypes.size() == expectedTypes.size());
    for (size_t i = 0; i < expectedTypes.size(); i++) {
        RC_ASSERT(actualTypes[i] == expectedTypes[i]);
    }

    /* Cleanup */
    htaccess_directives_free(parsed);
}
