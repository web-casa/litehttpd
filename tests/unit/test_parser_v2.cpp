/**
 * test_parser_v2.cpp - Unit tests for v2 IfModule parsing
 *
 * Tests IfModule block parsing: positive condition, negated condition,
 * nested blocks, unclosed blocks, and various directives inside.
 *
 * Validates: Requirements 3.1-3.7
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <string>

extern "C" {
#include "htaccess_directive.h"
#include "htaccess_parser.h"
#include "htaccess_printer.h"
}

#include "mock_lsiapi.h"

/* ---- Helper: parse a string ---- */
static htaccess_directive_t *parse(const char *input) {
    return htaccess_parse(input, strlen(input), "test");
}

/* ==================================================================
 *  IfModule parsing tests
 * ================================================================== */

/* Positive condition: <IfModule mod_rewrite.c> parses children */
TEST(ParserV2IfModule, PositiveConditionParsesChildren) {
    const char *input =
        "<IfModule mod_rewrite.c>\n"
        "Header set X-Powered-By OLS\n"
        "</IfModule>\n";

    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_IFMODULE);
    EXPECT_STREQ(dirs->name, "mod_rewrite.c");
    EXPECT_EQ(dirs->data.ifmodule.negated, 0);

    /* Should have one child */
    htaccess_directive_t *child = dirs->data.ifmodule.children;
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->type, DIR_HEADER_SET);
    EXPECT_STREQ(child->name, "X-Powered-By");
    EXPECT_STREQ(child->value, "OLS");
    EXPECT_EQ(child->next, nullptr);

    /* No more top-level directives */
    EXPECT_EQ(dirs->next, nullptr);

    htaccess_directives_free(dirs);
}

/* Negated condition: <IfModule !mod_xxx.c> sets negated flag, still parses children */
TEST(ParserV2IfModule, NegatedConditionSetsFlag) {
    const char *input =
        "<IfModule !mod_xxx.c>\n"
        "php_value memory_limit 256M\n"
        "</IfModule>\n";

    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_IFMODULE);
    EXPECT_STREQ(dirs->name, "!mod_xxx.c");
    EXPECT_EQ(dirs->data.ifmodule.negated, 1);

    /* Children should still be parsed */
    htaccess_directive_t *child = dirs->data.ifmodule.children;
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->type, DIR_PHP_VALUE);
    EXPECT_STREQ(child->name, "memory_limit");
    EXPECT_STREQ(child->value, "256M");

    htaccess_directives_free(dirs);
}

/* Nested IfModule blocks */
TEST(ParserV2IfModule, NestedIfModuleBlocks) {
    const char *input =
        "<IfModule mod_expires.c>\n"
        "ExpiresActive On\n"
        "<IfModule mod_headers.c>\n"
        "Header set Cache-Control public\n"
        "</IfModule>\n"
        "</IfModule>\n";

    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_IFMODULE);
    EXPECT_STREQ(dirs->name, "mod_expires.c");

    /* First child: ExpiresActive On */
    htaccess_directive_t *child1 = dirs->data.ifmodule.children;
    ASSERT_NE(child1, nullptr);
    EXPECT_EQ(child1->type, DIR_EXPIRES_ACTIVE);

    /* Second child: nested IfModule */
    htaccess_directive_t *child2 = child1->next;
    ASSERT_NE(child2, nullptr);
    EXPECT_EQ(child2->type, DIR_IFMODULE);
    EXPECT_STREQ(child2->name, "mod_headers.c");
    EXPECT_EQ(child2->data.ifmodule.negated, 0);

    /* Nested child: Header set */
    htaccess_directive_t *nested = child2->data.ifmodule.children;
    ASSERT_NE(nested, nullptr);
    EXPECT_EQ(nested->type, DIR_HEADER_SET);
    EXPECT_STREQ(nested->name, "Cache-Control");
    EXPECT_STREQ(nested->value, "public");

    htaccess_directives_free(dirs);
}

/* Unclosed IfModule block should be discarded with WARN log */
TEST(ParserV2IfModule, UnclosedBlockDiscardedWithWarn) {
    mock_lsiapi::reset_global_state();

    const char *input =
        "<IfModule mod_rewrite.c>\n"
        "Header set X-Test value\n";
    /* No </IfModule> closing tag */

    htaccess_directive_t *dirs = parse(input);
    /* Unclosed block should be discarded — no directives returned */
    EXPECT_EQ(dirs, nullptr);

    /* Verify a WARN log was emitted */
    const auto &logs = mock_lsiapi::get_log_records();
    bool found_warn = false;
    for (const auto &rec : logs) {
        if (rec.level == LSI_LOG_WARN &&
            rec.message.find("unclosed <IfModule>") != std::string::npos) {
            found_warn = true;
            break;
        }
    }
    EXPECT_TRUE(found_warn) << "Expected WARN log about unclosed IfModule block";

    htaccess_directives_free(dirs);
}

/* IfModule with various directives inside (Header, php_value, etc.) */
TEST(ParserV2IfModule, VariousDirectivesInside) {
    const char *input =
        "<IfModule mod_rewrite.c>\n"
        "Header set X-Frame-Options DENY\n"
        "php_value upload_max_filesize 64M\n"
        "php_flag display_errors on\n"
        "SetEnv APP_ENV production\n"
        "</IfModule>\n";

    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_IFMODULE);

    /* Walk children and verify types */
    htaccess_directive_t *c = dirs->data.ifmodule.children;
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->type, DIR_HEADER_SET);

    c = c->next;
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->type, DIR_PHP_VALUE);
    EXPECT_STREQ(c->name, "upload_max_filesize");

    c = c->next;
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->type, DIR_PHP_FLAG);
    EXPECT_STREQ(c->name, "display_errors");

    c = c->next;
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->type, DIR_SETENV);
    EXPECT_STREQ(c->name, "APP_ENV");

    EXPECT_EQ(c->next, nullptr);

    htaccess_directives_free(dirs);
}

/* IfModule with FilesMatch nested inside */
TEST(ParserV2IfModule, FilesMatchInsideIfModule) {
    const char *input =
        "<IfModule mod_headers.c>\n"
        "<FilesMatch \"\\.php$\">\n"
        "Header set X-Content-Type-Options nosniff\n"
        "</FilesMatch>\n"
        "</IfModule>\n";

    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_IFMODULE);

    /* Child should be a FilesMatch block */
    htaccess_directive_t *child = dirs->data.ifmodule.children;
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->type, DIR_FILES_MATCH);
    EXPECT_STREQ(child->data.files_match.pattern, "\\.php$");

    /* FilesMatch child */
    htaccess_directive_t *fm_child = child->data.files_match.children;
    ASSERT_NE(fm_child, nullptr);
    EXPECT_EQ(fm_child->type, DIR_HEADER_SET);

    htaccess_directives_free(dirs);
}

/* Multiple IfModule blocks at top level */
TEST(ParserV2IfModule, MultipleTopLevelIfModules) {
    const char *input =
        "<IfModule mod_expires.c>\n"
        "ExpiresActive On\n"
        "</IfModule>\n"
        "<IfModule mod_headers.c>\n"
        "Header set X-Test value\n"
        "</IfModule>\n";

    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_IFMODULE);
    EXPECT_STREQ(dirs->name, "mod_expires.c");

    htaccess_directive_t *second = dirs->next;
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->type, DIR_IFMODULE);
    EXPECT_STREQ(second->name, "mod_headers.c");

    EXPECT_EQ(second->next, nullptr);

    htaccess_directives_free(dirs);
}

/* IfModule with empty body */
TEST(ParserV2IfModule, EmptyBody) {
    const char *input =
        "<IfModule mod_rewrite.c>\n"
        "</IfModule>\n";

    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_IFMODULE);
    EXPECT_EQ(dirs->data.ifmodule.children, nullptr);

    htaccess_directives_free(dirs);
}

/* IfModule mixed with top-level directives */
TEST(ParserV2IfModule, MixedWithTopLevelDirectives) {
    const char *input =
        "Header set X-Before before\n"
        "<IfModule mod_rewrite.c>\n"
        "Header set X-Inside inside\n"
        "</IfModule>\n"
        "Header set X-After after\n";

    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    /* First: top-level Header */
    EXPECT_EQ(dirs->type, DIR_HEADER_SET);
    EXPECT_STREQ(dirs->name, "X-Before");

    /* Second: IfModule */
    htaccess_directive_t *im = dirs->next;
    ASSERT_NE(im, nullptr);
    EXPECT_EQ(im->type, DIR_IFMODULE);

    /* Third: top-level Header */
    htaccess_directive_t *after = im->next;
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->type, DIR_HEADER_SET);
    EXPECT_STREQ(after->name, "X-After");

    htaccess_directives_free(dirs);
}

/* ==================================================================
 *  Files block parsing tests — Validates: Requirements 5.1-5.5
 * ================================================================== */

/* Parse <Files wp-config.php> with children */
TEST(ParserV2Files, ParseFilesWithChildren) {
    const char *input =
        "<Files wp-config.php>\n"
        "Header set X-Protected true\n"
        "</Files>\n";

    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_FILES);
    EXPECT_STREQ(dirs->name, "wp-config.php");

    /* Should have one child */
    htaccess_directive_t *child = dirs->data.files.children;
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->type, DIR_HEADER_SET);
    EXPECT_STREQ(child->name, "X-Protected");
    EXPECT_STREQ(child->value, "true");
    EXPECT_EQ(child->next, nullptr);

    /* No more top-level directives */
    EXPECT_EQ(dirs->next, nullptr);

    htaccess_directives_free(dirs);
}

/* Parse <Files> with quoted filename */
TEST(ParserV2Files, ParseFilesQuotedFilename) {
    const char *input =
        "<Files \"wp-config.php\">\n"
        "Header set X-Secure yes\n"
        "</Files>\n";

    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_FILES);
    EXPECT_STREQ(dirs->name, "wp-config.php");

    htaccess_directive_t *child = dirs->data.files.children;
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->type, DIR_HEADER_SET);

    htaccess_directives_free(dirs);
}

/* Parse Files block with multiple children */
TEST(ParserV2Files, ParseFilesMultipleChildren) {
    const char *input =
        "<Files .htaccess>\n"
        "Header set X-First one\n"
        "Header set X-Second two\n"
        "Header set X-Third three\n"
        "</Files>\n";

    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_FILES);
    EXPECT_STREQ(dirs->name, ".htaccess");

    /* Walk children and count */
    int count = 0;
    for (htaccess_directive_t *c = dirs->data.files.children; c; c = c->next)
        count++;
    EXPECT_EQ(count, 3);

    htaccess_directives_free(dirs);
}

/* Files block inside IfModule */
TEST(ParserV2Files, FilesInsideIfModule) {
    const char *input =
        "<IfModule mod_headers.c>\n"
        "<Files wp-config.php>\n"
        "Header set X-Deny true\n"
        "</Files>\n"
        "</IfModule>\n";

    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_IFMODULE);
    EXPECT_STREQ(dirs->name, "mod_headers.c");

    /* IfModule child should be a Files block */
    htaccess_directive_t *child = dirs->data.ifmodule.children;
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->type, DIR_FILES);
    EXPECT_STREQ(child->name, "wp-config.php");

    /* Files child */
    htaccess_directive_t *fc = child->data.files.children;
    ASSERT_NE(fc, nullptr);
    EXPECT_EQ(fc->type, DIR_HEADER_SET);
    EXPECT_STREQ(fc->name, "X-Deny");

    htaccess_directives_free(dirs);
}

/* Unclosed Files block should be discarded with WARN */
TEST(ParserV2Files, UnclosedFilesBlockDiscardedWithWarn) {
    mock_lsiapi::reset_global_state();

    const char *input =
        "<Files secret.txt>\n"
        "Header set X-Test value\n";
    /* No </Files> closing tag */

    htaccess_directive_t *dirs = parse(input);
    /* Unclosed block should be discarded — no directives returned */
    EXPECT_EQ(dirs, nullptr);

    /* Verify a WARN log was emitted */
    const auto &logs = mock_lsiapi::get_log_records();
    bool found_warn = false;
    for (const auto &rec : logs) {
        if (rec.level == LSI_LOG_WARN &&
            rec.message.find("unclosed <Files>") != std::string::npos) {
            found_warn = true;
            break;
        }
    }
    EXPECT_TRUE(found_warn) << "Expected WARN log about unclosed Files block";

    htaccess_directives_free(dirs);
}

/* Empty Files block */
TEST(ParserV2Files, EmptyFilesBlock) {
    const char *input =
        "<Files empty.txt>\n"
        "</Files>\n";

    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_FILES);
    EXPECT_STREQ(dirs->name, "empty.txt");
    EXPECT_EQ(dirs->data.files.children, nullptr);

    htaccess_directives_free(dirs);
}

/* Files block mixed with top-level directives */
TEST(ParserV2Files, MixedWithTopLevelDirectives) {
    const char *input =
        "Header set X-Before before\n"
        "<Files wp-config.php>\n"
        "Header set X-Inside inside\n"
        "</Files>\n"
        "Header set X-After after\n";

    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    /* First: top-level Header */
    EXPECT_EQ(dirs->type, DIR_HEADER_SET);
    EXPECT_STREQ(dirs->name, "X-Before");

    /* Second: Files block */
    htaccess_directive_t *fb = dirs->next;
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->type, DIR_FILES);
    EXPECT_STREQ(fb->name, "wp-config.php");

    /* Third: top-level Header */
    htaccess_directive_t *after = fb->next;
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->type, DIR_HEADER_SET);
    EXPECT_STREQ(after->name, "X-After");

    htaccess_directives_free(dirs);
}

/* ==================================================================
 *  ExpiresDefault parsing tests
 * ================================================================== */

TEST(ParserV2ExpiresDefault, BasicParsing)
{
    const char *input = "ExpiresDefault \"access plus 1 month\"\n";
    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_EXPIRES_DEFAULT);
    EXPECT_STREQ(dirs->value, "access plus 1 month");
    EXPECT_EQ(dirs->data.expires.duration_sec, 2592000L);
    htaccess_directives_free(dirs);
}

TEST(ParserV2ExpiresDefault, CombinedDuration)
{
    const char *input = "ExpiresDefault \"access plus 1 year 6 months\"\n";
    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_EXPIRES_DEFAULT);
    EXPECT_EQ(dirs->data.expires.duration_sec, 31536000L + 15552000L);
    htaccess_directives_free(dirs);
}

TEST(ParserV2ExpiresDefault, RoundTrip)
{
    const char *input = "ExpiresDefault \"access plus 1 month\"\n";
    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    char *printed = htaccess_print(dirs);
    ASSERT_NE(printed, nullptr);

    htaccess_directive_t *dirs2 = parse(printed);
    ASSERT_NE(dirs2, nullptr);
    EXPECT_EQ(dirs2->type, DIR_EXPIRES_DEFAULT);
    EXPECT_STREQ(dirs2->value, dirs->value);
    EXPECT_EQ(dirs2->data.expires.duration_sec, dirs->data.expires.duration_sec);

    htaccess_directives_free(dirs);
    htaccess_directives_free(dirs2);
    free(printed);
}

TEST(ParserV2ExpiresDefault, FallbackBehavior)
{
    /* ExpiresDefault should be used when no ExpiresByType matches */
    const char *input =
        "ExpiresActive On\n"
        "ExpiresByType text/html \"access plus 1 hour\"\n"
        "ExpiresDefault \"access plus 1 month\"\n";
    htaccess_directive_t *dirs = parse(input);
    ASSERT_NE(dirs, nullptr);

    /* Verify all three directives parsed */
    EXPECT_EQ(dirs->type, DIR_EXPIRES_ACTIVE);
    ASSERT_NE(dirs->next, nullptr);
    EXPECT_EQ(dirs->next->type, DIR_EXPIRES_BY_TYPE);
    ASSERT_NE(dirs->next->next, nullptr);
    EXPECT_EQ(dirs->next->next->type, DIR_EXPIRES_DEFAULT);

    htaccess_directives_free(dirs);
}

/* ------------------------------------------------------------------ */
/*  Unclosed <If> block cleanup tests                                  */
/* ------------------------------------------------------------------ */

/* Unclosed <If> block should be discarded with WARN log */
TEST(ParserV2If, UnclosedIfBlockDiscardedWithWarn) {
    mock_lsiapi::reset_global_state();

    const char *input =
        "<If \"%{HTTPS} == 'on'\">\n"
        "Header set X-Inside \"yes\"\n";
        /* No </If> close tag */

    htaccess_directive_t *dirs = parse(input);
    /* Unclosed block should be discarded — no directives returned */
    EXPECT_EQ(dirs, nullptr);

    bool found_warn = false;
    const auto &logs = mock_lsiapi::get_log_records();
    for (const auto &rec : logs) {
        if (rec.level == LSI_LOG_WARN &&
            rec.message.find("unclosed <If>") != std::string::npos) {
            found_warn = true;
            break;
        }
    }
    EXPECT_TRUE(found_warn) << "Expected WARN log about unclosed If block";

    htaccess_directives_free(dirs);
}

/* Unclosed <ElseIf> should also be cleaned up */
TEST(ParserV2If, UnclosedElseIfBlockDiscardedWithWarn) {
    mock_lsiapi::reset_global_state();

    const char *input =
        "<If \"%{HTTPS} == 'on'\">\n"
        "Header set X-SSL \"yes\"\n"
        "</If>\n"
        "<ElseIf \"%{HTTPS} == 'off'\">\n"
        "Header set X-NoSSL \"yes\"\n";
        /* No </ElseIf> close tag */

    htaccess_directive_t *dirs = parse(input);
    /* The closed <If> should be returned; the unclosed <ElseIf> discarded */
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_IF);

    bool found_warn = false;
    const auto &logs2 = mock_lsiapi::get_log_records();
    for (const auto &rec : logs2) {
        if (rec.level == LSI_LOG_WARN &&
            rec.message.find("unclosed <If>") != std::string::npos) {
            found_warn = true;
            break;
        }
    }
    EXPECT_TRUE(found_warn) << "Expected WARN log about unclosed ElseIf block";

    htaccess_directives_free(dirs);
}

/* ------------------------------------------------------------------ */
/*  Orphaned ElseIf/Else rejection tests                               */
/* ------------------------------------------------------------------ */

TEST(ParserV2If, OrphanedElseIfRejectedWithWarn) {
    mock_lsiapi::reset_global_state();
    const char *input =
        "<ElseIf \"%{HTTPS} == 'on'\">\n"
        "Header set X-Bad yes\n"
        "</ElseIf>\n";
    htaccess_directive_t *dirs = parse(input);
    /* Orphaned ElseIf should be discarded */
    EXPECT_EQ(dirs, nullptr);

    bool found_warn = false;
    const auto &logs = mock_lsiapi::get_log_records();
    for (const auto &rec : logs) {
        if (rec.level == LSI_LOG_WARN &&
            rec.message.find("without preceding <If>") != std::string::npos) {
            found_warn = true;
            break;
        }
    }
    EXPECT_TRUE(found_warn) << "Expected WARN about orphaned ElseIf";
    htaccess_directives_free(dirs);
}

TEST(ParserV2If, OrphanedElseRejectedWithWarn) {
    mock_lsiapi::reset_global_state();
    const char *input =
        "<Else>\n"
        "Header set X-Bad yes\n"
        "</Else>\n";
    htaccess_directive_t *dirs = parse(input);
    EXPECT_EQ(dirs, nullptr);

    bool found_warn = false;
    const auto &logs = mock_lsiapi::get_log_records();
    for (const auto &rec : logs) {
        if (rec.level == LSI_LOG_WARN &&
            rec.message.find("without preceding <If>") != std::string::npos) {
            found_warn = true;
            break;
        }
    }
    EXPECT_TRUE(found_warn) << "Expected WARN about orphaned Else";
    htaccess_directives_free(dirs);
}

/* ------------------------------------------------------------------ */
/*  Close-tag mismatch rejection tests                                 */
/* ------------------------------------------------------------------ */

TEST(ParserV2If, CloseTagMismatchRejected) {
    mock_lsiapi::reset_global_state();
    const char *input =
        "<If \"%{HTTPS} == 'on'\">\n"
        "Header set X-SSL yes\n"
        "</ElseIf>\n";
    htaccess_directive_t *dirs = parse(input);
    /* Mismatched close tag should discard the block */

    bool found_warn = false;
    const auto &logs = mock_lsiapi::get_log_records();
    for (const auto &rec : logs) {
        if (rec.level == LSI_LOG_WARN &&
            rec.message.find("close tag does not match") != std::string::npos) {
            found_warn = true;
            break;
        }
    }
    EXPECT_TRUE(found_warn) << "Expected WARN about mismatched close tag";
    htaccess_directives_free(dirs);
}