/**
 * prop_ifmodule.cpp - Property-based tests for IfModule conditional inclusion
 *
 * Feature: htaccess-v2-enhancements, Property 28: IfModule conditional inclusion
 *
 * For any IfModule block, verify that positive conditions (no "!" prefix)
 * include children and negated conditions ("!" prefix) set the negated flag.
 *
 * **Validates: Requirements 3.3, 3.4**
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_parser.h"
#include "htaccess_printer.h"
#include "htaccess_directive.h"
}

/* ------------------------------------------------------------------ */
/*  Generators                                                         */
/* ------------------------------------------------------------------ */

namespace gen {

/**
 * Generate a random Apache module name (e.g., mod_rewrite.c, mod_headers.c).
 */
inline rc::Gen<std::string> moduleName()
{
    static const std::vector<std::string> kModules = {
        "mod_rewrite.c", "mod_headers.c", "mod_expires.c",
        "mod_deflate.c", "mod_ssl.c", "mod_setenvif.c",
        "mod_mime.c", "mod_auth_basic.c", "mod_authz_core.c",
        "mod_dir.c", "mod_autoindex.c", "mod_negotiation.c",
        "mod_filter.c", "mod_env.c", "mod_alias.c",
        "mod_cgi.c", "mod_php.c", "mod_security.c"
    };
    return rc::gen::elementOf(kModules);
}

/**
 * Generate a simple inner directive line that the parser can handle.
 * We use Header set directives since they are well-tested and reliable.
 */
inline rc::Gen<std::string> innerDirectiveLine()
{
    static const std::string kAlnum =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    return rc::gen::map(
        rc::gen::pair(
            rc::gen::mapcat(
                rc::gen::inRange(1, 10),
                [](int len) {
                    return rc::gen::container<std::vector<char>>(
                        (std::size_t)len, rc::gen::elementOf(kAlnum));
                }),
            rc::gen::mapcat(
                rc::gen::inRange(1, 10),
                [](int len) {
                    return rc::gen::container<std::vector<char>>(
                        (std::size_t)len, rc::gen::elementOf(kAlnum));
                })),
        [](const std::pair<std::vector<char>, std::vector<char>> &p) {
            std::string name = "X-" + std::string(p.first.begin(), p.first.end());
            std::string val(p.second.begin(), p.second.end());
            return "Header set " + name + " " + val;
        });
}

/**
 * Generate 1-5 inner directive lines.
 */
inline rc::Gen<std::vector<std::string>> innerDirectives()
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, 6),
        [](int count) {
            return rc::gen::container<std::vector<std::string>>(
                (std::size_t)count, innerDirectiveLine());
        });
}

} /* namespace gen */

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/** Count directives in a linked list. */
static int countDirectives(const htaccess_directive_t *head)
{
    int n = 0;
    for (const htaccess_directive_t *d = head; d; d = d->next)
        ++n;
    return n;
}

/** Build an IfModule .htaccess block string. */
static std::string buildIfModuleBlock(const std::string &moduleName,
                                      bool negated,
                                      const std::vector<std::string> &innerLines)
{
    std::string result;
    if (negated)
        result = "<IfModule !" + moduleName + ">\n";
    else
        result = "<IfModule " + moduleName + ">\n";

    for (const auto &line : innerLines)
        result += line + "\n";

    result += "</IfModule>\n";
    return result;
}

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class IfModulePropertyFixture : public ::testing::Test {
public:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
    }
};

/* ------------------------------------------------------------------ */
/*  Property 28: IfModule 条件包含 — Positive condition                */
/*                                                                     */
/*  For any IfModule block with a positive condition (no "!" prefix),  */
/*  the parser SHALL produce a DIR_IFMODULE node with negated=0 and    */
/*  children containing all enclosed directives.                       */
/*                                                                     */
/*  **Validates: Requirements 3.3**                                    */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(IfModulePropertyFixture,
                      PositiveConditionIncludesChildren,
                      ())
{
    auto modName = *gen::moduleName();
    auto innerLines = *gen::innerDirectives();

    std::string input = buildIfModuleBlock(modName, false, innerLines);

    htaccess_directive_t *dirs = htaccess_parse(input.c_str(), input.size(), "test");
    RC_ASSERT(dirs != nullptr);
    RC_ASSERT(dirs->type == DIR_IFMODULE);
    RC_ASSERT(dirs->data.ifmodule.negated == 0);
    RC_ASSERT(std::string(dirs->name) == modName);

    /* Children count must match the number of inner directive lines */
    int childCount = countDirectives(dirs->data.ifmodule.children);
    RC_ASSERT(childCount == (int)innerLines.size());

    /* Each child should be a DIR_HEADER_SET */
    for (htaccess_directive_t *c = dirs->data.ifmodule.children; c; c = c->next)
        RC_ASSERT(c->type == DIR_HEADER_SET);

    /* No sibling directives at top level */
    RC_ASSERT(dirs->next == nullptr);

    htaccess_directives_free(dirs);
}

/* ------------------------------------------------------------------ */
/*  Property 28: IfModule 条件包含 — Negated condition                 */
/*                                                                     */
/*  For any IfModule block with a negated condition ("!" prefix),      */
/*  the parser SHALL produce a DIR_IFMODULE node with negated=1 and    */
/*  children still parsed (for round-trip), but the negated flag       */
/*  signals the executor to skip them.                                 */
/*                                                                     */
/*  **Validates: Requirements 3.4**                                    */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(IfModulePropertyFixture,
                      NegatedConditionSetsFlag,
                      ())
{
    auto modName = *gen::moduleName();
    auto innerLines = *gen::innerDirectives();

    std::string input = buildIfModuleBlock(modName, true, innerLines);

    htaccess_directive_t *dirs = htaccess_parse(input.c_str(), input.size(), "test");
    RC_ASSERT(dirs != nullptr);
    RC_ASSERT(dirs->type == DIR_IFMODULE);
    RC_ASSERT(dirs->data.ifmodule.negated == 1);

    /* Name should include the "!" prefix */
    std::string expectedName = "!" + modName;
    RC_ASSERT(std::string(dirs->name) == expectedName);

    /* Children should still be parsed (for round-trip support) */
    int childCount = countDirectives(dirs->data.ifmodule.children);
    RC_ASSERT(childCount == (int)innerLines.size());

    /* No sibling directives at top level */
    RC_ASSERT(dirs->next == nullptr);

    htaccess_directives_free(dirs);
}

/* ------------------------------------------------------------------ */
/*  Property 28: IfModule round-trip preserves structure                */
/*                                                                     */
/*  For any IfModule block (positive or negated), parse → print →      */
/*  parse produces an equivalent DIR_IFMODULE node with the same       */
/*  negated flag and same number of children.                          */
/*                                                                     */
/*  **Validates: Requirements 3.3, 3.4**                               */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(IfModulePropertyFixture,
                      RoundTripPreservesStructure,
                      ())
{
    auto modName = *gen::moduleName();
    auto innerLines = *gen::innerDirectives();
    auto negated = *rc::gen::arbitrary<bool>();

    std::string input = buildIfModuleBlock(modName, negated, innerLines);

    /* First parse */
    htaccess_directive_t *dirs1 = htaccess_parse(input.c_str(), input.size(), "test");
    RC_ASSERT(dirs1 != nullptr);

    /* Print */
    char *printed = htaccess_print(dirs1);
    RC_ASSERT(printed != nullptr);

    /* Second parse */
    htaccess_directive_t *dirs2 = htaccess_parse(printed, strlen(printed), "test");
    RC_ASSERT(dirs2 != nullptr);

    /* Compare structure */
    RC_ASSERT(dirs2->type == DIR_IFMODULE);
    RC_ASSERT(dirs2->data.ifmodule.negated == dirs1->data.ifmodule.negated);
    RC_ASSERT(std::string(dirs2->name) == std::string(dirs1->name));

    int count1 = countDirectives(dirs1->data.ifmodule.children);
    int count2 = countDirectives(dirs2->data.ifmodule.children);
    RC_ASSERT(count1 == count2);

    htaccess_directives_free(dirs1);
    htaccess_directives_free(dirs2);
    free(printed);
}
