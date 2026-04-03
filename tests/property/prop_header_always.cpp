/**
 * prop_header_always.cpp - Property-based tests for Header always directives
 *
 * Feature: htaccess-v2-enhancements
 *
 * Property 31: Header always 全响应覆盖
 *   For any Header always set directive and any HTTP status code
 *   (including 4xx, 5xx error responses), the response contains
 *   the specified header name and value.
 *
 * Validates: Requirements 6.1, 6.2
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

class HeaderAlwaysPropertyFixture : public ::testing::Test {
public:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
        exec_header_reset_shadow();
    }
protected:
    MockSession session_;
};

/* ------------------------------------------------------------------ */
/*  Property 31: Header always set works on ALL HTTP status codes      */
/*                                                                     */
/*  For any header name, value, and HTTP status code (including 4xx,   */
/*  5xx), Header always set places the header in the response.         */
/*                                                                     */
/*  **Validates: Requirements 6.1, 6.2**                               */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(HeaderAlwaysPropertyFixture,
                      HeaderAlwaysSetWorksOnAllStatusCodes,
                      ())
{
    auto name = *gen::headerName();
    auto val = *gen::headerValue();
    /* Generate any HTTP status code including errors */
    auto status = *rc::gen::elementOf(
        std::vector<int>{200, 201, 301, 302, 400, 401, 403, 404, 500, 502, 503});

    session_.set_status_code(status);

    auto *dir = make_dir(DIR_HEADER_ALWAYS_SET, name, val);
    int rc = exec_header(session_.handle(), dir);
    RC_ASSERT(rc == LSI_OK);

    RC_ASSERT(session_.has_response_header(name));
    RC_ASSERT(session_.get_response_header(name) == val);

    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Header always unset works on error responses                       */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(HeaderAlwaysPropertyFixture,
                      HeaderAlwaysUnsetWorksOnAllStatusCodes,
                      ())
{
    auto name = *gen::headerName();
    auto val = *gen::headerValue();
    auto status = *rc::gen::elementOf(
        std::vector<int>{200, 400, 403, 404, 500, 502, 503});

    session_.set_status_code(status);
    session_.add_response_header(name, val);

    auto *dir = make_dir(DIR_HEADER_ALWAYS_UNSET, name);
    int rc = exec_header(session_.handle(), dir);
    RC_ASSERT(rc == LSI_OK);

    RC_ASSERT(!session_.has_response_header(name));

    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Header always append works on error responses                      */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(HeaderAlwaysPropertyFixture,
                      HeaderAlwaysAppendWorksOnAllStatusCodes,
                      ())
{
    auto name = *gen::headerName();
    auto v1 = *gen::headerValue();
    auto v2 = *gen::headerValue();
    auto status = *rc::gen::elementOf(
        std::vector<int>{200, 400, 404, 500, 503});

    session_.set_status_code(status);
    session_.add_response_header(name, v1);

    auto *dir = make_dir(DIR_HEADER_ALWAYS_APPEND, name, v2);
    int rc = exec_header(session_.handle(), dir);
    RC_ASSERT(rc == LSI_OK);

    std::string result = session_.get_response_header(name);
    std::string expected = v1 + ", " + v2;
    RC_ASSERT(result == expected);

    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Header always merge is idempotent on error responses               */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(HeaderAlwaysPropertyFixture,
                      HeaderAlwaysMergeIdempotentOnAllStatusCodes,
                      ())
{
    auto name = *gen::headerName();
    auto val = *gen::headerValue();
    auto status = *rc::gen::elementOf(
        std::vector<int>{200, 400, 404, 500, 503});

    session_.set_status_code(status);

    auto *dir = make_dir(DIR_HEADER_ALWAYS_MERGE, name, val);

    int rc1 = exec_header(session_.handle(), dir);
    RC_ASSERT(rc1 == LSI_OK);
    std::string after_first = session_.get_response_header(name);

    int rc2 = exec_header(session_.handle(), dir);
    RC_ASSERT(rc2 == LSI_OK);
    std::string after_second = session_.get_response_header(name);

    RC_ASSERT(after_first == after_second);

    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Header always add accumulates on error responses                   */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(HeaderAlwaysPropertyFixture,
                      HeaderAlwaysAddAccumulatesOnAllStatusCodes,
                      ())
{
    auto name = *gen::headerName();
    auto val = *gen::headerValue();
    auto status = *rc::gen::elementOf(
        std::vector<int>{200, 400, 404, 500, 503});

    session_.set_status_code(status);

    int before = session_.count_response_headers(name);

    auto *dir = make_dir(DIR_HEADER_ALWAYS_ADD, name, val);
    int rc = exec_header(session_.handle(), dir);
    RC_ASSERT(rc == LSI_OK);

    int after = session_.count_response_headers(name);
    RC_ASSERT(after == before + 1);

    free_dir(dir);
}
