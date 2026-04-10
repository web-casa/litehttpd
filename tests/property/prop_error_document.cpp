/**
 * prop_error_document.cpp - Property-based tests for ErrorDocument executor
 *
 * Feature: ols-htaccess-module
 *
 * Property 17: ErrorDocument 外部 URL 重定向
 * Property 18: ErrorDocument 文本消息
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <cstring>
#include <cstdlib>

#include "mock_lsiapi.h"
#include "gen_header.h"

extern "C" {
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

/** Generate a URI path segment (alphanumeric, 1-8 chars). */
inline rc::Gen<std::string> pathSegment()
{
    static const std::string kPathChars =
        "abcdefghijklmnopqrstuvwxyz0123456789";
    return rc::gen::mapcat(
        rc::gen::inRange(1, 9),
        [](int len) {
            return rc::gen::map(
                rc::gen::container<std::vector<char>>(
                    (std::size_t)len,
                    rc::gen::elementOf(kPathChars)),
                [](const std::vector<char> &v) {
                    return std::string(v.begin(), v.end());
                });
        });
}

/** Generate an external URL (http:// or https://). */
inline rc::Gen<std::string> externalUrl()
{
    return rc::gen::map(
        rc::gen::tuple(
            rc::gen::elementOf(std::vector<std::string>{"http://", "https://"}),
            pathSegment(),
            pathSegment()),
        [](const std::tuple<std::string, std::string, std::string> &t) {
            return std::get<0>(t) + std::get<1>(t) + ".example.com/" +
                   std::get<2>(t);
        });
}

/** Generate a text message (printable ASCII, no quotes, 1-50 chars). */
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

class ErrorDocPropertyFixture : public ::testing::Test {
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
/*  Property 17: ErrorDocument 外部 URL 重定向                         */
/*                                                                     */
/*  For any HTTP error code and external URL, ErrorDocument directive   */
/*  should produce a 302 redirect to that URL.                         */
/*                                                                     */
/*  **Validates: Requirement 8.2**                                     */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(ErrorDocPropertyFixture,
                      ExternalUrlProduces302Redirect,
                      ())
{
    auto errorCode = *gen::errorCode();
    auto url = *gen::externalUrl();

    /* Set session status to match the error code */
    session_.set_status_code(errorCode);

    auto *dir = make_error_doc_dir(errorCode, url);
    int rc = exec_error_document(session_.handle(), dir);
    RC_ASSERT(rc == 0);

    /* Status should be changed to 302 */
    RC_ASSERT(session_.get_status_code() == 302);

    /* Location header should be set to the external URL */
    RC_ASSERT(session_.has_response_header("Location"));
    RC_ASSERT(session_.get_response_header("Location") == url);

    free_dir(dir);
}

/* Verify no action when status doesn't match */
RC_GTEST_FIXTURE_PROP(ErrorDocPropertyFixture,
                      ExternalUrlNoActionOnMismatch,
                      ())
{
    auto errorCode = *gen::errorCode();
    auto url = *gen::externalUrl();

    /* Set a different status code (200 OK) */
    session_.set_status_code(200);

    auto *dir = make_error_doc_dir(errorCode, url);
    int rc = exec_error_document(session_.handle(), dir);
    RC_ASSERT(rc == 0);

    /* Status should remain 200 — no redirect */
    RC_ASSERT(session_.get_status_code() == 200);
    RC_ASSERT(!session_.has_response_header("Location"));

    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Property 18: ErrorDocument 文本消息                                */
/*                                                                     */
/*  For any HTTP error code and quoted text message, ErrorDocument      */
/*  directive should return the text as the response body.             */
/*                                                                     */
/*  **Validates: Requirement 8.3**                                     */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(ErrorDocPropertyFixture,
                      QuotedTextSetAsResponseBody,
                      ())
{
    auto errorCode = *gen::errorCode();
    auto text = *gen::textMessage();

    /* Wrap text in quotes as the directive value */
    std::string quotedValue = "\"" + text + "\"";

    /* Set session status to match the error code */
    session_.set_status_code(errorCode);

    auto *dir = make_error_doc_dir(errorCode, quotedValue);
    int rc = exec_error_document(session_.handle(), dir);
    RC_ASSERT(rc == 0);

    /* Response body should be the unquoted text */
    RC_ASSERT(session_.get_resp_body() == text);

    /* Status should remain the original error code (not changed) */
    RC_ASSERT(session_.get_status_code() == errorCode);

    free_dir(dir);
}

/* Verify no action when status doesn't match for text message */
RC_GTEST_FIXTURE_PROP(ErrorDocPropertyFixture,
                      QuotedTextNoActionOnMismatch,
                      ())
{
    auto errorCode = *gen::errorCode();
    auto text = *gen::textMessage();

    std::string quotedValue = "\"" + text + "\"";

    /* Set a different status code */
    session_.set_status_code(200);

    auto *dir = make_error_doc_dir(errorCode, quotedValue);
    int rc = exec_error_document(session_.handle(), dir);
    RC_ASSERT(rc == 0);

    /* Response body should be empty — no action taken */
    RC_ASSERT(session_.get_resp_body().empty());

    free_dir(dir);
}
