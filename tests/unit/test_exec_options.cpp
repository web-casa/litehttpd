/**
 * test_exec_options.cpp - Unit tests for Options directive executor
 *
 * Tests parsing of +/-Indexes, +/-FollowSymLinks flags, multi-flag
 * combinations, unknown flag handling, and parse→print→parse round-trip.
 *
 * Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5
 */
#include <gtest/gtest.h>
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
/*  Helper                                                             */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_options(int indexes, int follow_symlinks,
                                          int multiviews = 0, int exec_cgi = 0)
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

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class ExecOptionsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
    MockSession session_;
};

/* ------------------------------------------------------------------ */
/*  -Indexes disables directory listing (Req 4.1)                      */
/* ------------------------------------------------------------------ */

TEST_F(ExecOptionsTest, MinusIndexesDisablesDirectoryListing)
{
    auto *dir = make_options(-1, 0);
    EXPECT_EQ(exec_options(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_dir_option("Indexes"), 0);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  +Indexes enables directory listing (Req 4.2)                       */
/* ------------------------------------------------------------------ */

TEST_F(ExecOptionsTest, PlusIndexesEnablesDirectoryListing)
{
    auto *dir = make_options(1, 0);
    EXPECT_EQ(exec_options(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_dir_option("Indexes"), 1);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  +FollowSymLinks enables symlink following (Req 4.3)                */
/* ------------------------------------------------------------------ */

TEST_F(ExecOptionsTest, PlusFollowSymLinksEnables)
{
    auto *dir = make_options(0, 1);
    EXPECT_EQ(exec_options(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_dir_option("FollowSymLinks"), 1);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  -FollowSymLinks disables symlink following (Req 4.3)               */
/* ------------------------------------------------------------------ */

TEST_F(ExecOptionsTest, MinusFollowSymLinksDisables)
{
    auto *dir = make_options(0, -1);
    EXPECT_EQ(exec_options(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_dir_option("FollowSymLinks"), 0);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Multiple flags: Options -Indexes +FollowSymLinks (Req 4.4)        */
/* ------------------------------------------------------------------ */

TEST_F(ExecOptionsTest, MultipleFlagsCombination)
{
    auto *dir = make_options(-1, 1);
    EXPECT_EQ(exec_options(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_dir_option("Indexes"), 0);
    EXPECT_EQ(session_.get_dir_option("FollowSymLinks"), 1);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Unchanged flags (0) are not applied                                */
/* ------------------------------------------------------------------ */

TEST_F(ExecOptionsTest, UnchangedFlagsNotApplied)
{
    auto *dir = make_options(0, 0, 0, 0);
    EXPECT_EQ(exec_options(session_.handle(), dir), LSI_OK);
    /* dir_options_ map should have no entries for unchanged flags */
    /* get_dir_option returns -1 for unset options */
    EXPECT_EQ(session_.get_dir_option("Indexes"), -1);
    EXPECT_EQ(session_.get_dir_option("FollowSymLinks"), -1);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Unknown flags are ignored with WARN logged (Req 4.5)               */
/* ------------------------------------------------------------------ */

TEST_F(ExecOptionsTest, UnknownFlagsIgnoredWithWarn)
{
    /* Parse "Options -Indexes +UnknownFlag" via the parser */
    const char *input = "Options -Indexes +UnknownFlag\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_OPTIONS);

    /* The known flag should be parsed correctly */
    EXPECT_EQ(dirs->data.options.indexes, -1);

    /* Execute — should succeed despite unknown flag */
    EXPECT_EQ(exec_options(session_.handle(), dirs), LSI_OK);
    EXPECT_EQ(session_.get_dir_option("Indexes"), 0);

    /* Verify WARN was logged for the unknown flag */
    const auto &logs = mock_lsiapi::get_log_records();
    bool found_warn = false;
    for (const auto &rec : logs) {
        if (rec.level == LSI_LOG_WARN &&
            rec.message.find("UnknownFlag") != std::string::npos) {
            found_warn = true;
            break;
        }
    }
    EXPECT_TRUE(found_warn) << "Expected WARN log for unknown Options flag";

    htaccess_directives_free(dirs);
}

/* ------------------------------------------------------------------ */
/*  Parsing: Options -Indexes +FollowSymLinks produces correct values  */
/* ------------------------------------------------------------------ */

TEST_F(ExecOptionsTest, ParseProducesCorrectTriState)
{
    const char *input = "Options -Indexes +FollowSymLinks\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_OPTIONS);
    EXPECT_EQ(dirs->data.options.indexes, -1);
    EXPECT_EQ(dirs->data.options.follow_symlinks, 1);
    EXPECT_EQ(dirs->data.options.multiviews, 0);
    EXPECT_EQ(dirs->data.options.exec_cgi, 0);
    ASSERT_NE(dirs->value, nullptr);
    EXPECT_STREQ(dirs->value, "-Indexes +FollowSymLinks");
    htaccess_directives_free(dirs);
}

/* ------------------------------------------------------------------ */
/*  Round-trip: parse → print → parse                                  */
/* ------------------------------------------------------------------ */

TEST_F(ExecOptionsTest, RoundTripParsesPrintParses)
{
    const char *input = "Options -Indexes +FollowSymLinks\n";
    htaccess_directive_t *d1 = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(d1, nullptr);

    char *printed = htaccess_print(d1);
    ASSERT_NE(printed, nullptr);

    htaccess_directive_t *d2 = htaccess_parse(printed, strlen(printed), "test2");
    ASSERT_NE(d2, nullptr);

    /* Verify equivalence */
    EXPECT_EQ(d2->type, d1->type);
    EXPECT_EQ(d2->data.options.indexes, d1->data.options.indexes);
    EXPECT_EQ(d2->data.options.follow_symlinks, d1->data.options.follow_symlinks);
    EXPECT_EQ(d2->data.options.multiviews, d1->data.options.multiviews);
    EXPECT_EQ(d2->data.options.exec_cgi, d1->data.options.exec_cgi);

    htaccess_directives_free(d1);
    htaccess_directives_free(d2);
    free(printed);
}

/* ------------------------------------------------------------------ */
/*  Edge cases                                                         */
/* ------------------------------------------------------------------ */

TEST_F(ExecOptionsTest, NullSessionReturnsError)
{
    auto *dir = make_options(-1, 1);
    EXPECT_EQ(exec_options(nullptr, dir), LSI_ERROR);
    free_dir(dir);
}

TEST_F(ExecOptionsTest, NullDirectiveReturnsError)
{
    EXPECT_EQ(exec_options(session_.handle(), nullptr), LSI_ERROR);
}

TEST_F(ExecOptionsTest, WrongTypeReturnsError)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_PHP_VALUE;
    EXPECT_EQ(exec_options(session_.handle(), d), LSI_ERROR);
    free(d);
}

TEST_F(ExecOptionsTest, AllFourFlagsApplied)
{
    auto *dir = make_options(1, -1, 1, -1);
    EXPECT_EQ(exec_options(session_.handle(), dir), LSI_OK);
    EXPECT_EQ(session_.get_dir_option("Indexes"), 1);
    EXPECT_EQ(session_.get_dir_option("FollowSymLinks"), 0);
    EXPECT_EQ(session_.get_dir_option("MultiViews"), 1);
    /* ExecCGI is blocked in .htaccess (LSWS behavior) — not applied */
    EXPECT_EQ(session_.get_dir_option("ExecCGI"), -1);
    free_dir(dir);
}
