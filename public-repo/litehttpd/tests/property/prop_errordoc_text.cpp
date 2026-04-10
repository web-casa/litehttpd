/**
 * prop_errordoc_text.cpp - Property-based tests for ErrorDocument text message pipeline
 *
 * Feature: htaccess-v2-enhancements, Property 27: ErrorDocument text message pipeline
 *
 * For any ErrorDocument value starting with a double quote, verify the
 * parse → exec pipeline sets the unquoted text as the response body.
 *
 * **Validates: Requirements 2.1, 2.2**
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
#include "htaccess_exec_error_doc.h"
#include "htaccess_directive.h"
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_error_doc_dir(int error_code,
                                                const std::string &value)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ERROR_DOCUMENT;
    d->line_number = 1;
    d->name = nullptr;
    d->value = strdup(value.c_str());
    d->data.error_doc.error_code = error_code;
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
/*  Generators                                                         */
/* ------------------------------------------------------------------ */

namespace gen {

/** Generate a valid HTTP error code (4xx or 5xx). */
inline rc::Gen<int> errorCode()
{
    return rc::gen::elementOf(std::vector<int>{
        400, 401, 403, 404, 405, 408, 410, 413, 414, 429,
        500, 501, 502, 503, 504});
}

/**
 * Generate a text message suitable for ErrorDocument.
 * Printable ASCII, no double quotes or backslashes, 1-50 chars.
 */
inline rc::Gen<std::string> textMessage()
{
    static const std::string kTextChars =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789 !#$%&()*+,-./:;<=>?@[]^_{|}~";
    return rc::gen::mapcat(
        rc::gen::inRange(1, 51),
        [](int len) {
            return rc::gen::map(
                rc::gen::container<std::vector<char>>(
                    (std::size_t)len,
                    rc::gen::elementOf(kTextChars)),
                [](const std::vector<char> &v) {
                    return std::string(v.begin(), v.end());
                });
        });
}

} /* namespace gen */

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class ErrorDocTextPropertyFixture : public ::testing::Test {
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
/*  Property 27: ErrorDocument 文本消息管道                             */
/*                                                                     */
/*  For any ErrorDocument value starting with a double quote, the      */
/*  parse → exec pipeline sets the unquoted text as the response body. */
/*                                                                     */
/*  We test two aspects:                                               */
/*  (a) Parser preserves the leading quote in the parsed value         */
/*  (b) Executor detects the quote and sets unquoted text as body      */
/*                                                                     */
/*  **Validates: Requirements 2.1, 2.2**                               */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(ErrorDocTextPropertyFixture,
                      ParseExecPipelineSetsUnquotedTextAsBody,
                      ())
{
    auto errorCode = *gen::errorCode();
    auto text = *gen::textMessage();

    /* Build the .htaccess line: ErrorDocument <code> "<text>" */
    std::string line = "ErrorDocument " + std::to_string(errorCode) +
                       " \"" + text + "\"\n";

    /* Step 1: Parse */
    htaccess_directive_t *dirs = htaccess_parse(line.c_str(), line.size(), "test");
    RC_ASSERT(dirs != nullptr);
    RC_ASSERT(dirs->type == DIR_ERROR_DOCUMENT);
    RC_ASSERT(dirs->data.error_doc.error_code == errorCode);
    RC_ASSERT(dirs->value != nullptr);

    /* The parser must preserve the leading quote */
    RC_ASSERT(dirs->value[0] == '"');

    /* Step 2: Execute — set session status to match the error code */
    session_.set_status_code(errorCode);
    int rc = exec_error_document(session_.handle(), dirs);
    RC_ASSERT(rc == 0);

    /* The response body should be the unquoted text */
    RC_ASSERT(session_.get_resp_body() == text);

    /* Status should remain the original error code (not redirected) */
    RC_ASSERT(session_.get_status_code() == errorCode);

    htaccess_directives_free(dirs);
}
