/**
 * prop_options.cpp - Property-based tests for Options flag execution
 *
 * Feature: htaccess-v2-enhancements, Property 29: Options flag execution
 *
 * For any Options flag combination (+/-Indexes, +/-FollowSymLinks, etc.),
 * verify that after execution, LSIAPI query results match the directive
 * flags: +Flag → enabled (1), -Flag → disabled (0).
 *
 * Uses RapidCheck + Google Test with random tri-state flag combinations.
 * At least 100 iterations.
 *
 * **Validates: Requirements 4.1, 4.2, 4.3, 4.4**
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <cstring>
#include <cstdlib>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_options.h"
#include "htaccess_directive.h"
#include "htaccess_parser.h"
#include "htaccess_printer.h"
}

/* ------------------------------------------------------------------ */
/*  Generators                                                         */
/* ------------------------------------------------------------------ */

namespace gen {

/**
 * Generate a tri-state flag value: -1 (disable), 0 (unchanged), +1 (enable).
 */
inline rc::Gen<int> triState()
{
    return rc::gen::elementOf(std::vector<int>{-1, 0, 1});
}

/**
 * Generate a random Options flag combination as a tuple of 4 tri-state values:
 * (indexes, follow_symlinks, multiviews, exec_cgi).
 */
inline rc::Gen<std::tuple<int, int, int, int>> optionsFlagCombo()
{
    return rc::gen::tuple(triState(), triState(), triState(), triState());
}

} /* namespace gen */

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/** Create an Options directive from tri-state flag values. */
static htaccess_directive_t *make_options(int indexes, int follow_symlinks,
                                          int multiviews, int exec_cgi)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_OPTIONS;
    d->line_number = 1;
    d->name = nullptr;
    d->value = nullptr;
    d->data.options.indexes = indexes;
    d->data.options.follow_symlinks = follow_symlinks;
    d->data.options.multiviews = multiviews;
    d->data.options.exec_cgi = exec_cgi;
    d->next = nullptr;
    return d;
}

static void free_dir(htaccess_directive_t *d)
{
    if (!d) return;
    free(d->name);
    free(d->value);
    free(d);
}

/**
 * Build an Options directive text string from tri-state flags.
 * Returns empty string if all flags are 0 (unchanged).
 */
static std::string buildOptionsText(int indexes, int follow_symlinks,
                                    int multiviews, int exec_cgi)
{
    std::string flags;
    if (indexes == 1)          flags += " +Indexes";
    else if (indexes == -1)    flags += " -Indexes";
    if (follow_symlinks == 1)  flags += " +FollowSymLinks";
    else if (follow_symlinks == -1) flags += " -FollowSymLinks";
    if (multiviews == 1)       flags += " +MultiViews";
    else if (multiviews == -1) flags += " -MultiViews";
    if (exec_cgi == 1)         flags += " +ExecCGI";
    else if (exec_cgi == -1)   flags += " -ExecCGI";

    if (flags.empty())
        return "";
    return "Options" + flags + "\n";
}

/**
 * Check a single flag's expected LSIAPI result.
 * tri_state +1 → get_dir_option returns 1 (enabled)
 * tri_state -1 → get_dir_option returns 0 (disabled)
 * tri_state  0 → get_dir_option returns -1 (unset/unchanged)
 */
static int expectedDirOption(int tri_state)
{
    if (tri_state > 0)  return 1;
    if (tri_state < 0)  return 0;
    return -1; /* unchanged → not set in mock */
}

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class OptionsPropertyFixture : public ::testing::Test {
public:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }

protected:
    MockSession session_;
};

/* ------------------------------------------------------------------ */
/*  Property 29: Options flag execution — direct struct                */
/*                                                                     */
/*  For any Options flag combination (+/-Indexes, +/-FollowSymLinks,   */
/*  +/-MultiViews, +/-ExecCGI), verify that after exec_options(),      */
/*  LSIAPI query results match: +Flag → 1, -Flag → 0, 0 → unset.     */
/*                                                                     */
/*  **Validates: Requirements 4.1, 4.2, 4.3, 4.4**                    */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(OptionsPropertyFixture,
                      FlagCombinationMatchesLSIAPI,
                      ())
{
    auto [indexes, follow_symlinks, multiviews, exec_cgi] =
        *gen::optionsFlagCombo();

    auto *dir = make_options(indexes, follow_symlinks, multiviews, exec_cgi);

    int rc = exec_options(session_.handle(), dir);
    RC_ASSERT(rc == LSI_OK);

    RC_ASSERT(session_.get_dir_option("Indexes") == expectedDirOption(indexes));
    RC_ASSERT(session_.get_dir_option("FollowSymLinks") == expectedDirOption(follow_symlinks));
    RC_ASSERT(session_.get_dir_option("MultiViews") == expectedDirOption(multiviews));
    RC_ASSERT(session_.get_dir_option("ExecCGI") == -1); /* blocked in .htaccess */

    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Property 29: Options flag execution — parse path                   */
/*                                                                     */
/*  For any non-trivial Options flag combination, verify that parsing  */
/*  the text then executing produces the same LSIAPI results.          */
/*                                                                     */
/*  **Validates: Requirements 4.1, 4.2, 4.3, 4.4**                    */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(OptionsPropertyFixture,
                      ParsedFlagCombinationMatchesLSIAPI,
                      ())
{
    auto [indexes, follow_symlinks, multiviews, exec_cgi] =
        *gen::optionsFlagCombo();

    /* Skip all-zero combos since they produce no Options directive text */
    RC_PRE(indexes != 0 || follow_symlinks != 0 ||
           multiviews != 0 || exec_cgi != 0);

    std::string text = buildOptionsText(indexes, follow_symlinks,
                                        multiviews, exec_cgi);
    RC_ASSERT(!text.empty());

    htaccess_directive_t *dirs = htaccess_parse(text.c_str(), text.size(), "test");
    RC_ASSERT(dirs != nullptr);
    RC_ASSERT(dirs->type == DIR_OPTIONS);

    /* Verify parsed tri-state values match input */
    RC_ASSERT(dirs->data.options.indexes == indexes);
    RC_ASSERT(dirs->data.options.follow_symlinks == follow_symlinks);
    RC_ASSERT(dirs->data.options.multiviews == multiviews);
    RC_ASSERT(dirs->data.options.exec_cgi == exec_cgi);

    /* Execute and verify LSIAPI state */
    int rc = exec_options(session_.handle(), dirs);
    RC_ASSERT(rc == LSI_OK);

    RC_ASSERT(session_.get_dir_option("Indexes") == expectedDirOption(indexes));
    RC_ASSERT(session_.get_dir_option("FollowSymLinks") == expectedDirOption(follow_symlinks));
    RC_ASSERT(session_.get_dir_option("MultiViews") == expectedDirOption(multiviews));
    RC_ASSERT(session_.get_dir_option("ExecCGI") == -1); /* blocked in .htaccess */

    htaccess_directives_free(dirs);
}

/* ------------------------------------------------------------------ */
/*  Property 29: Options round-trip preserves flags                    */
/*                                                                     */
/*  For any non-trivial Options flag combination, parse → print →      */
/*  parse produces equivalent tri-state values.                        */
/*                                                                     */
/*  **Validates: Requirements 4.1, 4.2, 4.3, 4.4**                    */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(OptionsPropertyFixture,
                      RoundTripPreservesFlags,
                      ())
{
    auto [indexes, follow_symlinks, multiviews, exec_cgi] =
        *gen::optionsFlagCombo();

    RC_PRE(indexes != 0 || follow_symlinks != 0 ||
           multiviews != 0 || exec_cgi != 0);

    std::string text = buildOptionsText(indexes, follow_symlinks,
                                        multiviews, exec_cgi);

    /* First parse */
    htaccess_directive_t *d1 = htaccess_parse(text.c_str(), text.size(), "test");
    RC_ASSERT(d1 != nullptr);

    /* Print */
    char *printed = htaccess_print(d1);
    RC_ASSERT(printed != nullptr);

    /* Second parse */
    htaccess_directive_t *d2 = htaccess_parse(printed, strlen(printed), "test2");
    RC_ASSERT(d2 != nullptr);

    /* Verify equivalence */
    RC_ASSERT(d2->type == DIR_OPTIONS);
    RC_ASSERT(d2->data.options.indexes == d1->data.options.indexes);
    RC_ASSERT(d2->data.options.follow_symlinks == d1->data.options.follow_symlinks);
    RC_ASSERT(d2->data.options.multiviews == d1->data.options.multiviews);
    RC_ASSERT(d2->data.options.exec_cgi == d1->data.options.exec_cgi);

    htaccess_directives_free(d1);
    htaccess_directives_free(d2);
    free(printed);
}
