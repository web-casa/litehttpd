/**
 * test_errordoc_quote_fix.cpp - Unit tests for ErrorDocument quote preservation fix
 *
 * Verifies that:
 * 1. ErrorDocument 404 "Custom message" parses with value starting with "
 * 2. The executor detects the quote prefix and returns unquoted text as body
 * 3. External URL and local file path modes are not affected
 *
 * Validates: Requirements 2.1, 2.2, 2.3, 2.4, 2.5
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_parser.h"
#include "htaccess_printer.h"
#include "htaccess_exec_error_doc.h"
#include "htaccess_directive.h"
}

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class ErrorDocQuoteFixTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }

    MockSession session_;
};

/* ------------------------------------------------------------------ */
/*  Parser: quoted text value preserves leading quote                   */
/* ------------------------------------------------------------------ */

TEST_F(ErrorDocQuoteFixTest, ParsePreservesLeadingQuote)
{
    const char *input = "ErrorDocument 404 \"Custom not found\"\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    EXPECT_EQ(dirs->type, DIR_ERROR_DOCUMENT);
    EXPECT_EQ(dirs->data.error_doc.error_code, 404);
    ASSERT_NE(dirs->value, nullptr);
    /* The leading quote must be preserved so the executor can detect text mode */
    EXPECT_EQ(dirs->value[0], '"');
    htaccess_directives_free(dirs);
}

TEST_F(ErrorDocQuoteFixTest, ParsePreservesFullQuotedString)
{
    const char *input = "ErrorDocument 500 \"Internal Server Error\"\n";
    htaccess_directive_t *dirs = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs, nullptr);
    ASSERT_NE(dirs->value, nullptr);
    EXPECT_EQ(dirs->value[0], '"');
    /* Value should contain the full quoted string */
    EXPECT_TRUE(strstr(dirs->value, "Internal Server Error") != nullptr);
    htaccess_directives_free(dirs);
}

/* ------------------------------------------------------------------ */
/*  Executor: quoted text mode returns unquoted body                   */
/* ------------------------------------------------------------------ */

TEST_F(ErrorDocQuoteFixTest, ExecutorReturnsUnquotedTextAsBody)
{
    /* Build a directive manually with leading quote (as parser should produce) */
    auto *dir = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    dir->type = DIR_ERROR_DOCUMENT;
    dir->line_number = 1;
    dir->name = nullptr;
    dir->value = strdup("\"Custom not found\"");
    dir->data.error_doc.error_code = 404;
    dir->next = nullptr;

    session_.set_status_code(404);
    EXPECT_EQ(exec_error_document(session_.handle(), dir), 0);

    /* The response body should be the unquoted text */
    EXPECT_EQ(session_.get_resp_body(), "Custom not found");

    /* Status should remain 404 (not redirected) */
    EXPECT_EQ(session_.get_status_code(), 404);

    free(dir->value);
    free(dir);
}

TEST_F(ErrorDocQuoteFixTest, ExecutorHandlesQuotedTextWithoutTrailingQuote)
{
    auto *dir = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    dir->type = DIR_ERROR_DOCUMENT;
    dir->line_number = 1;
    dir->name = nullptr;
    dir->value = strdup("\"Custom message without closing quote");
    dir->data.error_doc.error_code = 403;
    dir->next = nullptr;

    session_.set_status_code(403);
    EXPECT_EQ(exec_error_document(session_.handle(), dir), 0);

    /* Should still return the text after the leading quote */
    EXPECT_EQ(session_.get_resp_body(), "Custom message without closing quote");

    free(dir->value);
    free(dir);
}

/* ------------------------------------------------------------------ */
/*  External URL mode is not affected                                  */
/* ------------------------------------------------------------------ */

TEST_F(ErrorDocQuoteFixTest, ExternalUrlModeUnaffected)
{
    auto *dir = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    dir->type = DIR_ERROR_DOCUMENT;
    dir->line_number = 1;
    dir->name = nullptr;
    dir->value = strdup("https://example.com/error.html");
    dir->data.error_doc.error_code = 404;
    dir->next = nullptr;

    session_.set_status_code(404);
    EXPECT_EQ(exec_error_document(session_.handle(), dir), 0);

    /* Should redirect to the URL */
    EXPECT_EQ(session_.get_status_code(), 302);
    EXPECT_EQ(session_.get_response_header("Location"),
              "https://example.com/error.html");
    /* Response body should be empty (redirect, not text) */
    EXPECT_TRUE(session_.get_resp_body().empty());

    free(dir->value);
    free(dir);
}

TEST_F(ErrorDocQuoteFixTest, HttpUrlModeUnaffected)
{
    auto *dir = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    dir->type = DIR_ERROR_DOCUMENT;
    dir->line_number = 1;
    dir->name = nullptr;
    dir->value = strdup("http://example.com/404.html");
    dir->data.error_doc.error_code = 404;
    dir->next = nullptr;

    session_.set_status_code(404);
    EXPECT_EQ(exec_error_document(session_.handle(), dir), 0);

    EXPECT_EQ(session_.get_status_code(), 302);
    EXPECT_EQ(session_.get_response_header("Location"),
              "http://example.com/404.html");

    free(dir->value);
    free(dir);
}

/* ------------------------------------------------------------------ */
/*  Local file path mode is not affected                               */
/* ------------------------------------------------------------------ */

TEST_F(ErrorDocQuoteFixTest, LocalFilePathModeUnaffected)
{
    auto *dir = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    dir->type = DIR_ERROR_DOCUMENT;
    dir->line_number = 1;
    dir->name = nullptr;
    dir->value = strdup("/errors/404.html");
    dir->data.error_doc.error_code = 404;
    dir->next = nullptr;

    session_.set_status_code(404);
    EXPECT_EQ(exec_error_document(session_.handle(), dir), 0);

    /* Status should remain 404 (not redirected) */
    EXPECT_EQ(session_.get_status_code(), 404);
    /* No Location header */
    EXPECT_FALSE(session_.has_response_header("Location"));
    /* Body should be empty (local file is a stub) */
    EXPECT_TRUE(session_.get_resp_body().empty());

    free(dir->value);
    free(dir);
}

/* ------------------------------------------------------------------ */
/*  Parse → Print → Parse round-trip for quoted ErrorDocument          */
/* ------------------------------------------------------------------ */

TEST_F(ErrorDocQuoteFixTest, RoundTripPreservesQuotedText)
{
    const char *input = "ErrorDocument 404 \"Custom not found\"\n";
    htaccess_directive_t *dirs1 = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(dirs1, nullptr);

    char *printed = htaccess_print(dirs1);
    ASSERT_NE(printed, nullptr);

    htaccess_directive_t *dirs2 = htaccess_parse(printed, strlen(printed), "test");
    ASSERT_NE(dirs2, nullptr);

    /* Both parses should produce equivalent directives */
    EXPECT_EQ(dirs1->type, dirs2->type);
    EXPECT_EQ(dirs1->data.error_doc.error_code, dirs2->data.error_doc.error_code);
    ASSERT_NE(dirs2->value, nullptr);
    EXPECT_EQ(dirs2->value[0], '"');
    EXPECT_STREQ(dirs1->value, dirs2->value);

    free(printed);
    htaccess_directives_free(dirs1);
    htaccess_directives_free(dirs2);
}

/* ------------------------------------------------------------------ */
/*  Error code mismatch — no action taken                              */
/* ------------------------------------------------------------------ */

TEST_F(ErrorDocQuoteFixTest, ErrorCodeMismatchNoAction)
{
    auto *dir = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    dir->type = DIR_ERROR_DOCUMENT;
    dir->line_number = 1;
    dir->name = nullptr;
    dir->value = strdup("\"Not found\"");
    dir->data.error_doc.error_code = 404;
    dir->next = nullptr;

    /* Current status is 200, not 404 */
    session_.set_status_code(200);
    EXPECT_EQ(exec_error_document(session_.handle(), dir), 0);

    /* Nothing should have changed */
    EXPECT_EQ(session_.get_status_code(), 200);
    EXPECT_TRUE(session_.get_resp_body().empty());

    free(dir->value);
    free(dir);
}

/* ------------------------------------------------------------------ */
/*  CRLF injection rejection in external redirect URL                  */
/* ------------------------------------------------------------------ */

TEST_F(ErrorDocQuoteFixTest, ExternalRedirectRejectsCRLF)
{
    auto *dir = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    dir->type = DIR_ERROR_DOCUMENT;
    dir->line_number = 1;
    dir->name = nullptr;
    dir->value = strdup("https://evil.com\r\nSet-Cookie: stolen=1");
    dir->data.error_doc.error_code = 404;
    dir->next = nullptr;

    session_.set_status_code(404);
    EXPECT_EQ(exec_error_document(session_.handle(), dir), -1);

    /* Verify no Location header was written and status unchanged */
    EXPECT_EQ(session_.get_status_code(), 404);
    EXPECT_TRUE(session_.get_response_header("Location").empty());

    free(dir->value);
    free(dir);
}

TEST_F(ErrorDocQuoteFixTest, ExternalRedirectRejectsLF)
{
    auto *dir = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    dir->type = DIR_ERROR_DOCUMENT;
    dir->line_number = 1;
    dir->name = nullptr;
    dir->value = strdup("http://evil.com/path\nEvil: header");
    dir->data.error_doc.error_code = 500;
    dir->next = nullptr;

    session_.set_status_code(500);
    EXPECT_EQ(exec_error_document(session_.handle(), dir), -1);

    /* Verify no Location header was written and status unchanged */
    EXPECT_EQ(session_.get_status_code(), 500);
    EXPECT_TRUE(session_.get_response_header("Location").empty());

    free(dir->value);
    free(dir);
}

/* ------------------------------------------------------------------ */
/*  Case-insensitive URL scheme detection                              */
/* ------------------------------------------------------------------ */

TEST_F(ErrorDocQuoteFixTest, ExternalRedirectUppercaseScheme)
{
    auto *dir = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    dir->type = DIR_ERROR_DOCUMENT;
    dir->line_number = 1;
    dir->name = nullptr;
    dir->value = strdup("HTTPS://example.com/error.html");
    dir->data.error_doc.error_code = 500;
    dir->next = nullptr;

    session_.set_status_code(500);
    EXPECT_EQ(exec_error_document(session_.handle(), dir), 0);
    EXPECT_EQ(session_.get_status_code(), 302);
    EXPECT_EQ(session_.get_response_header("Location"),
              "HTTPS://example.com/error.html");

    free(dir->value);
    free(dir);
}

TEST_F(ErrorDocQuoteFixTest, ExternalRedirectMixedCaseScheme)
{
    auto *dir = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    dir->type = DIR_ERROR_DOCUMENT;
    dir->line_number = 1;
    dir->name = nullptr;
    dir->value = strdup("Http://example.com/404");
    dir->data.error_doc.error_code = 404;
    dir->next = nullptr;

    session_.set_status_code(404);
    EXPECT_EQ(exec_error_document(session_.handle(), dir), 0);
    EXPECT_EQ(session_.get_status_code(), 302);

    free(dir->value);
    free(dir);
}
