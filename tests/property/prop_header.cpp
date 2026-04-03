/**
 * prop_header.cpp - Property-based tests for Header/RequestHeader executors
 *
 * Feature: ols-htaccess-module
 *
 * Property 5: Header set 替换语义
 * Property 6: Header unset 移除语义
 * Property 7: Header append 追加语义
 * Property 8: Header merge 幂等性
 * Property 9: Header add 累加语义
 * Property 10: RequestHeader set/unset 语义
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
#include "htaccess_exec_header.h"
#include "htaccess_directive.h"
}

/* ------------------------------------------------------------------ */
/*  Helper: create a directive on the stack-ish (malloc'd for safety)  */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_dir(directive_type_t type,
                                      const std::string &name,
                                      const std::string &value = "")
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = type;
    d->line_number = 1;
    d->name = strdup(name.c_str());
    d->value = value.empty() ? nullptr : strdup(value.c_str());
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

class HeaderPropertyFixture : public ::testing::Test {
public:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
        exec_header_reset_shadow();
    }

    void TearDown() override {}

protected:
    MockSession session_;
};

/* ------------------------------------------------------------------ */
/*  Property 5: Header set 替换语义                                    */
/*                                                                     */
/*  For any header name and value, after Header set, the response has  */
/*  exactly one value for that header equal to the specified value.     */
/*                                                                     */
/*  **Validates: Requirement 4.1**                                     */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(HeaderPropertyFixture,
                      HeaderSetReplacesExistingValue,
                      ())
{
    auto name = *gen::headerName();
    auto oldVal = *gen::headerValue();
    auto newVal = *gen::headerValue();

    /* Pre-populate with an existing value */
    session_.add_response_header(name, oldVal);

    auto *dir = make_dir(DIR_HEADER_SET, name, newVal);
    int rc = exec_header(session_.handle(), dir);
    RC_ASSERT(rc == LSI_OK);

    /* Should have exactly one value equal to newVal */
    RC_ASSERT(session_.count_response_headers(name) == 1);
    RC_ASSERT(session_.get_response_header(name) == newVal);

    free_dir(dir);
}


/* ------------------------------------------------------------------ */
/*  Property 6: Header unset 移除语义                                  */
/*                                                                     */
/*  For any existing response header, after Header unset, the response */
/*  no longer contains that header.                                    */
/*                                                                     */
/*  **Validates: Requirement 4.2**                                     */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(HeaderPropertyFixture,
                      HeaderUnsetRemovesHeader,
                      ())
{
    auto name = *gen::headerName();
    auto val = *gen::headerValue();

    /* Pre-populate */
    session_.add_response_header(name, val);
    RC_ASSERT(session_.has_response_header(name));

    auto *dir = make_dir(DIR_HEADER_UNSET, name);
    int rc = exec_header(session_.handle(), dir);
    RC_ASSERT(rc == LSI_OK);

    RC_ASSERT(!session_.has_response_header(name));
    RC_ASSERT(session_.count_response_headers(name) == 0);

    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Property 7: Header append 追加语义                                 */
/*                                                                     */
/*  For any existing header value V1 and append value V2, after        */
/*  Header append, the header value contains V1 and V2 comma-separated.*/
/*                                                                     */
/*  **Validates: Requirement 4.3**                                     */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(HeaderPropertyFixture,
                      HeaderAppendAddsCommaSeparated,
                      ())
{
    auto name = *gen::headerName();
    auto v1 = *gen::headerValue();
    auto v2 = *gen::headerValue();

    /* Pre-populate with v1 */
    session_.add_response_header(name, v1);

    auto *dir = make_dir(DIR_HEADER_APPEND, name, v2);
    int rc = exec_header(session_.handle(), dir);
    RC_ASSERT(rc == LSI_OK);

    std::string result = session_.get_response_header(name);
    /* Result should contain both v1 and v2 separated by ", " */
    std::string expected = v1 + ", " + v2;
    RC_ASSERT(result == expected);

    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Property 8: Header merge 幂等性                                    */
/*                                                                     */
/*  Executing Header merge twice with the same value produces the same */
/*  result as once (value appears only once).                          */
/*                                                                     */
/*  **Validates: Requirement 4.4**                                     */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(HeaderPropertyFixture,
                      HeaderMergeIsIdempotent,
                      ())
{
    auto name = *gen::headerName();
    auto val = *gen::headerValue();

    auto *dir = make_dir(DIR_HEADER_MERGE, name, val);

    /* First merge */
    int rc1 = exec_header(session_.handle(), dir);
    RC_ASSERT(rc1 == LSI_OK);
    std::string after_first = session_.get_response_header(name);

    /* Second merge with same value */
    int rc2 = exec_header(session_.handle(), dir);
    RC_ASSERT(rc2 == LSI_OK);
    std::string after_second = session_.get_response_header(name);

    /* Result should be identical */
    RC_ASSERT(after_first == after_second);

    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Property 9: Header add 累加语义                                    */
/*                                                                     */
/*  After Header add, the count of response headers with that name     */
/*  increases by 1.                                                    */
/*                                                                     */
/*  **Validates: Requirement 4.5**                                     */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(HeaderPropertyFixture,
                      HeaderAddIncreasesCount,
                      ())
{
    auto name = *gen::headerName();
    auto val = *gen::headerValue();

    /* Optionally pre-populate with some existing headers */
    auto preCount = *rc::gen::inRange(0, 4);
    for (int i = 0; i < preCount; i++) {
        auto pv = *gen::headerValue();
        session_.add_response_header(name, pv);
    }

    int before = session_.count_response_headers(name);

    auto *dir = make_dir(DIR_HEADER_ADD, name, val);
    int rc = exec_header(session_.handle(), dir);
    RC_ASSERT(rc == LSI_OK);

    int after = session_.count_response_headers(name);
    RC_ASSERT(after == before + 1);

    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Property 10: RequestHeader set/unset 语义                          */
/*                                                                     */
/*  RequestHeader set sets the request header to the specified value;  */
/*  RequestHeader unset removes the request header.                    */
/*                                                                     */
/*  **Validates: Requirements 4.6, 4.7**                               */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(HeaderPropertyFixture,
                      RequestHeaderSetThenUnset,
                      ())
{
    auto name = *gen::headerName();
    auto val = *gen::headerValue();

    /* RequestHeader set */
    auto *setDir = make_dir(DIR_REQUEST_HEADER_SET, name, val);
    int rc1 = exec_request_header(session_.handle(), setDir);
    RC_ASSERT(rc1 == LSI_OK);
    RC_ASSERT(session_.has_request_header(name));
    RC_ASSERT(session_.get_request_header(name) == val);

    /* RequestHeader unset */
    auto *unsetDir = make_dir(DIR_REQUEST_HEADER_UNSET, name);
    int rc2 = exec_request_header(session_.handle(), unsetDir);
    RC_ASSERT(rc2 == LSI_OK);
    RC_ASSERT(!session_.has_request_header(name));

    free_dir(setDir);
    free_dir(unsetDir);
}
