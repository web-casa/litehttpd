/**
 * test_printer_v2.cpp - Unit tests for v2 IfModule printing
 *
 * Tests IfModule block printing and round-trip (parse → print → parse).
 *
 * Validates: Requirements 3.1-3.7
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <string>

extern "C" {
#include "htaccess_directive.h"
#include "htaccess_printer.h"
#include "htaccess_parser.h"
}

/* ---- Helper: allocate a directive with strdup'd name/value ---- */

static htaccess_directive_t *make_dir(directive_type_t type,
                                      const char *name,
                                      const char *value,
                                      int line = 1) {
    auto *d = static_cast<htaccess_directive_t *>(
        calloc(1, sizeof(htaccess_directive_t)));
    d->type = type;
    d->line_number = line;
    if (name)  d->name  = strdup(name);
    if (value) d->value = strdup(value);
    d->next = nullptr;
    return d;
}

/* ==================================================================
 *  IfModule printing tests
 * ================================================================== */

/* Print IfModule block with children */
TEST(PrinterV2IfModule, PrintIfModuleWithChildren) {
    auto *im = make_dir(DIR_IFMODULE, "mod_rewrite.c", nullptr);
    im->data.ifmodule.negated = 0;

    auto *child1 = make_dir(DIR_HEADER_SET, "X-Frame-Options", "DENY", 2);
    auto *child2 = make_dir(DIR_PHP_VALUE, "memory_limit", "256M", 3);
    child1->next = child2;
    im->data.ifmodule.children = child1;

    char *out = htaccess_print(im);
    ASSERT_NE(out, nullptr);

    std::string expected =
        "<IfModule mod_rewrite.c>\n"
        "Header set X-Frame-Options DENY\n"
        "php_value memory_limit 256M\n"
        "</IfModule>\n";
    EXPECT_EQ(std::string(out), expected);

    free(out);
    htaccess_directives_free(im);
}

/* Print negated IfModule block */
TEST(PrinterV2IfModule, PrintNegatedIfModule) {
    auto *im = make_dir(DIR_IFMODULE, "!mod_xxx.c", nullptr);
    im->data.ifmodule.negated = 1;

    auto *child = make_dir(DIR_HEADER_SET, "X-Test", "value", 2);
    im->data.ifmodule.children = child;

    char *out = htaccess_print(im);
    ASSERT_NE(out, nullptr);

    std::string expected =
        "<IfModule !mod_xxx.c>\n"
        "Header set X-Test value\n"
        "</IfModule>\n";
    EXPECT_EQ(std::string(out), expected);

    free(out);
    htaccess_directives_free(im);
}

/* Print empty IfModule block */
TEST(PrinterV2IfModule, PrintEmptyIfModule) {
    auto *im = make_dir(DIR_IFMODULE, "mod_rewrite.c", nullptr);
    im->data.ifmodule.negated = 0;
    im->data.ifmodule.children = nullptr;

    char *out = htaccess_print(im);
    ASSERT_NE(out, nullptr);

    std::string expected =
        "<IfModule mod_rewrite.c>\n"
        "</IfModule>\n";
    EXPECT_EQ(std::string(out), expected);

    free(out);
    htaccess_directives_free(im);
}

/* ==================================================================
 *  Round-trip tests: parse → print → parse
 * ================================================================== */

/* Helper: compare two directive lists for equivalence */
static void assert_directives_equal(const htaccess_directive_t *a,
                                    const htaccess_directive_t *b) {
    while (a && b) {
        EXPECT_EQ(a->type, b->type);
        if (a->name && b->name)
            EXPECT_STREQ(a->name, b->name);
        else
            EXPECT_EQ(a->name == nullptr, b->name == nullptr);
        if (a->value && b->value)
            EXPECT_STREQ(a->value, b->value);
        else
            EXPECT_EQ(a->value == nullptr, b->value == nullptr);

        /* Recurse into IfModule children */
        if (a->type == DIR_IFMODULE) {
            EXPECT_EQ(a->data.ifmodule.negated, b->data.ifmodule.negated);
            assert_directives_equal(a->data.ifmodule.children,
                                    b->data.ifmodule.children);
        }

        a = a->next;
        b = b->next;
    }
    EXPECT_EQ(a, nullptr);
    EXPECT_EQ(b, nullptr);
}

/* Round-trip: simple IfModule */
TEST(PrinterV2IfModule, RoundTripSimpleIfModule) {
    const char *input =
        "<IfModule mod_rewrite.c>\n"
        "Header set X-Powered-By OLS\n"
        "</IfModule>\n";

    htaccess_directive_t *parsed = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(parsed, nullptr);

    char *printed = htaccess_print(parsed);
    ASSERT_NE(printed, nullptr);

    htaccess_directive_t *reparsed = htaccess_parse(printed, strlen(printed), "test2");
    ASSERT_NE(reparsed, nullptr);

    assert_directives_equal(parsed, reparsed);

    free(printed);
    htaccess_directives_free(parsed);
    htaccess_directives_free(reparsed);
}

/* Round-trip: negated IfModule */
TEST(PrinterV2IfModule, RoundTripNegatedIfModule) {
    const char *input =
        "<IfModule !mod_xxx.c>\n"
        "php_value memory_limit 256M\n"
        "</IfModule>\n";

    htaccess_directive_t *parsed = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(parsed, nullptr);

    char *printed = htaccess_print(parsed);
    ASSERT_NE(printed, nullptr);

    htaccess_directive_t *reparsed = htaccess_parse(printed, strlen(printed), "test2");
    ASSERT_NE(reparsed, nullptr);

    assert_directives_equal(parsed, reparsed);

    free(printed);
    htaccess_directives_free(parsed);
    htaccess_directives_free(reparsed);
}

/* Round-trip: nested IfModule blocks */
TEST(PrinterV2IfModule, RoundTripNestedIfModule) {
    const char *input =
        "<IfModule mod_expires.c>\n"
        "ExpiresActive On\n"
        "<IfModule mod_headers.c>\n"
        "Header set Cache-Control public\n"
        "</IfModule>\n"
        "</IfModule>\n";

    htaccess_directive_t *parsed = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(parsed, nullptr);

    char *printed = htaccess_print(parsed);
    ASSERT_NE(printed, nullptr);

    htaccess_directive_t *reparsed = htaccess_parse(printed, strlen(printed), "test2");
    ASSERT_NE(reparsed, nullptr);

    assert_directives_equal(parsed, reparsed);

    free(printed);
    htaccess_directives_free(parsed);
    htaccess_directives_free(reparsed);
}

/* Round-trip: IfModule with multiple directive types */
TEST(PrinterV2IfModule, RoundTripMultipleDirectiveTypes) {
    const char *input =
        "<IfModule mod_rewrite.c>\n"
        "Header set X-Frame-Options DENY\n"
        "php_value upload_max_filesize 64M\n"
        "Order Allow,Deny\n"
        "Allow from 192.168.1.0/24\n"
        "</IfModule>\n";

    htaccess_directive_t *parsed = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(parsed, nullptr);

    char *printed = htaccess_print(parsed);
    ASSERT_NE(printed, nullptr);

    htaccess_directive_t *reparsed = htaccess_parse(printed, strlen(printed), "test2");
    ASSERT_NE(reparsed, nullptr);

    assert_directives_equal(parsed, reparsed);

    free(printed);
    htaccess_directives_free(parsed);
    htaccess_directives_free(reparsed);
}

/* ==================================================================
 *  Files block printing tests — Validates: Requirements 5.1-5.5
 * ================================================================== */

/* Print Files block with children */
TEST(PrinterV2Files, PrintFilesWithChildren) {
    auto *fb = make_dir(DIR_FILES, "wp-config.php", nullptr);
    auto *child1 = make_dir(DIR_HEADER_SET, "X-Protected", "true", 2);
    auto *child2 = make_dir(DIR_HEADER_SET, "X-Secure", "yes", 3);
    child1->next = child2;
    fb->data.files.children = child1;

    char *out = htaccess_print(fb);
    ASSERT_NE(out, nullptr);

    std::string expected =
        "<Files wp-config.php>\n"
        "Header set X-Protected true\n"
        "Header set X-Secure yes\n"
        "</Files>\n";
    EXPECT_EQ(std::string(out), expected);

    free(out);
    htaccess_directives_free(fb);
}

/* Print empty Files block */
TEST(PrinterV2Files, PrintEmptyFiles) {
    auto *fb = make_dir(DIR_FILES, "empty.txt", nullptr);
    fb->data.files.children = nullptr;

    char *out = htaccess_print(fb);
    ASSERT_NE(out, nullptr);

    std::string expected =
        "<Files empty.txt>\n"
        "</Files>\n";
    EXPECT_EQ(std::string(out), expected);

    free(out);
    htaccess_directives_free(fb);
}

/* ==================================================================
 *  Files round-trip tests: parse → print → parse
 * ================================================================== */

/* Helper: compare Files directive lists for equivalence */
static void assert_files_directives_equal(const htaccess_directive_t *a,
                                          const htaccess_directive_t *b) {
    while (a && b) {
        EXPECT_EQ(a->type, b->type);
        if (a->name && b->name)
            EXPECT_STREQ(a->name, b->name);
        else
            EXPECT_EQ(a->name == nullptr, b->name == nullptr);
        if (a->value && b->value)
            EXPECT_STREQ(a->value, b->value);
        else
            EXPECT_EQ(a->value == nullptr, b->value == nullptr);

        /* Recurse into container children */
        if (a->type == DIR_FILES) {
            assert_files_directives_equal(a->data.files.children,
                                          b->data.files.children);
        }
        if (a->type == DIR_IFMODULE) {
            EXPECT_EQ(a->data.ifmodule.negated, b->data.ifmodule.negated);
            assert_files_directives_equal(a->data.ifmodule.children,
                                          b->data.ifmodule.children);
        }

        a = a->next;
        b = b->next;
    }
    EXPECT_EQ(a, nullptr);
    EXPECT_EQ(b, nullptr);
}

/* Round-trip: simple Files block */
TEST(PrinterV2Files, RoundTripSimpleFiles) {
    const char *input =
        "<Files wp-config.php>\n"
        "Header set X-Protected true\n"
        "</Files>\n";

    htaccess_directive_t *parsed = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(parsed, nullptr);

    char *printed = htaccess_print(parsed);
    ASSERT_NE(printed, nullptr);

    htaccess_directive_t *reparsed = htaccess_parse(printed, strlen(printed), "test2");
    ASSERT_NE(reparsed, nullptr);

    assert_files_directives_equal(parsed, reparsed);

    free(printed);
    htaccess_directives_free(parsed);
    htaccess_directives_free(reparsed);
}

/* Round-trip: Files block with multiple children */
TEST(PrinterV2Files, RoundTripFilesMultipleChildren) {
    const char *input =
        "<Files .htaccess>\n"
        "Header set X-First one\n"
        "Header set X-Second two\n"
        "</Files>\n";

    htaccess_directive_t *parsed = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(parsed, nullptr);

    char *printed = htaccess_print(parsed);
    ASSERT_NE(printed, nullptr);

    htaccess_directive_t *reparsed = htaccess_parse(printed, strlen(printed), "test2");
    ASSERT_NE(reparsed, nullptr);

    assert_files_directives_equal(parsed, reparsed);

    free(printed);
    htaccess_directives_free(parsed);
    htaccess_directives_free(reparsed);
}

/* Round-trip: Files block inside IfModule */
TEST(PrinterV2Files, RoundTripFilesInsideIfModule) {
    const char *input =
        "<IfModule mod_headers.c>\n"
        "<Files wp-config.php>\n"
        "Header set X-Deny true\n"
        "</Files>\n"
        "</IfModule>\n";

    htaccess_directive_t *parsed = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(parsed, nullptr);

    char *printed = htaccess_print(parsed);
    ASSERT_NE(printed, nullptr);

    htaccess_directive_t *reparsed = htaccess_parse(printed, strlen(printed), "test2");
    ASSERT_NE(reparsed, nullptr);

    assert_files_directives_equal(parsed, reparsed);

    free(printed);
    htaccess_directives_free(parsed);
    htaccess_directives_free(reparsed);
}
