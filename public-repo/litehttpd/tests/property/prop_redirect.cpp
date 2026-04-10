/**
 * prop_redirect.cpp - Property-based tests for Redirect/RedirectMatch executors
 *
 * Feature: ols-htaccess-module
 *
 * Property 14: Redirect 响应正确性
 * Property 15: RedirectMatch 捕获组替换
 * Property 16: Redirect 短路执行
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
#include "htaccess_exec_redirect.h"
#include "htaccess_exec_header.h"
#include "htaccess_directive.h"
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_redirect_dir(const std::string &prefix,
                                               const std::string &target,
                                               int status_code)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_REDIRECT;
    d->line_number = 1;
    d->name = strdup(prefix.c_str());
    d->value = strdup(target.c_str());
    d->data.redirect.status_code = status_code;
    d->data.redirect.pattern = nullptr;
    d->next = nullptr;
    return d;
}

static htaccess_directive_t *make_redirect_match_dir(const std::string &pattern,
                                                     const std::string &target,
                                                     int status_code)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_REDIRECT_MATCH;
    d->line_number = 1;
    d->name = nullptr;
    d->value = strdup(target.c_str());
    d->data.redirect.status_code = status_code;
    d->data.redirect.pattern = strdup(pattern.c_str());
    d->next = nullptr;
    return d;
}

static htaccess_directive_t *make_header_dir(const std::string &name,
                                             const std::string &value)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_HEADER_SET;
    d->line_number = 2;
    d->name = strdup(name.c_str());
    d->value = strdup(value.c_str());
    d->next = nullptr;
    return d;
}

static void free_dir(htaccess_directive_t *d)
{
    if (!d) return;
    free(d->name);
    free(d->value);
    if (d->type == DIR_REDIRECT_MATCH)
        free(d->data.redirect.pattern);
    free(d);
}

static void free_dir_chain(htaccess_directive_t *d)
{
    while (d) {
        htaccess_directive_t *next = d->next;
        free_dir(d);
        d = next;
    }
}

/* ------------------------------------------------------------------ */
/*  Generators                                                         */
/* ------------------------------------------------------------------ */

namespace gen {

/** Generate a valid redirect status code (301, 302, 303, 307, 308). */
inline rc::Gen<int> redirectStatus()
{
    return rc::gen::elementOf(std::vector<int>{301, 302, 303, 307, 308});
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

/** Generate a URI path like /foo or /foo/bar. */
inline rc::Gen<std::string> uriPath()
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, 4),
        [](int depth) {
            return rc::gen::map(
                rc::gen::container<std::vector<std::string>>(
                    (std::size_t)depth, pathSegment()),
                [](const std::vector<std::string> &segs) {
                    std::string path = "/";
                    for (size_t i = 0; i < segs.size(); i++) {
                        if (i > 0) path += "/";
                        path += segs[i];
                    }
                    return path;
                });
        });
}

/** Generate a target URL (absolute URL). */
inline rc::Gen<std::string> targetUrl()
{
    return rc::gen::map(
        rc::gen::pair(pathSegment(), uriPath()),
        [](const std::pair<std::string, std::string> &p) {
            return "https://" + p.first + ".example.com" + p.second;
        });
}

} /* namespace gen */

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class RedirectPropertyFixture : public ::testing::Test {
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
/*  Property 14: Redirect 响应正确性                                   */
/*                                                                     */
/*  For any HTTP status code and target URL, after executing a         */
/*  Redirect directive, the response status equals the specified code  */
/*  and the Location header equals the target URL.                     */
/*                                                                     */
/*  **Validates: Requirement 7.1**                                     */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(RedirectPropertyFixture,
                      RedirectSetsStatusAndLocation,
                      ())
{
    auto prefix = *gen::uriPath();
    auto target = *gen::targetUrl();
    auto status = *gen::redirectStatus();

    /* Build a URI that starts with the prefix + tail */
    auto suffix = *gen::pathSegment();
    std::string uri = prefix + "/" + suffix;
    std::string expected_location = target + "/" + suffix;

    session_.set_request_uri(uri);

    auto *dir = make_redirect_dir(prefix, target, status);
    int rc = exec_redirect(session_.handle(), dir);
    RC_ASSERT(rc == 1);

    RC_ASSERT(session_.get_status_code() == status);
    /* Apache mod_alias appends the unmatched URI tail to the target */
    RC_ASSERT(session_.get_response_header("Location") == expected_location);

    free_dir(dir);
}

/* Also test default 302 when status_code is 0 */
RC_GTEST_FIXTURE_PROP(RedirectPropertyFixture,
                      RedirectDefaultsTo302,
                      ())
{
    auto prefix = *gen::uriPath();
    auto target = *gen::targetUrl();

    auto suffix = *gen::pathSegment();
    std::string uri = prefix + "/" + suffix;
    std::string expected_location = target + "/" + suffix;

    session_.set_request_uri(uri);

    auto *dir = make_redirect_dir(prefix, target, 0);
    int rc = exec_redirect(session_.handle(), dir);
    RC_ASSERT(rc == 1);

    RC_ASSERT(session_.get_status_code() == 302);
    RC_ASSERT(session_.get_response_header("Location") == expected_location);

    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Property 15: RedirectMatch 捕获组替换                              */
/*                                                                     */
/*  For a regex with capture groups and a matching URI, the Location   */
/*  header has $N replaced with the corresponding capture values.      */
/*                                                                     */
/*  **Validates: Requirements 7.3, 7.4**                               */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(RedirectPropertyFixture,
                      RedirectMatchSubstitutesBackrefs,
                      ())
{
    /* Generate two path segments to use as capture groups */
    auto seg1 = *gen::pathSegment();
    auto seg2 = *gen::pathSegment();

    /* Build a regex that captures two segments: ^/([a-z0-9]+)/([a-z0-9]+) */
    std::string pattern = "^/([a-z0-9]+)/([a-z0-9]+)";
    std::string uri = "/" + seg1 + "/" + seg2;

    /* Target URL template with backreferences */
    std::string tmpl = "https://new.example.com/$2/$1";
    std::string expected = "https://new.example.com/" + seg2 + "/" + seg1;

    auto status = *gen::redirectStatus();

    session_.set_request_uri(uri);

    auto *dir = make_redirect_match_dir(pattern, tmpl, status);
    int rc = exec_redirect_match(session_.handle(), dir);
    RC_ASSERT(rc == 1);

    RC_ASSERT(session_.get_status_code() == status);
    RC_ASSERT(session_.get_response_header("Location") == expected);

    free_dir(dir);
}

/* Test single capture group */
RC_GTEST_FIXTURE_PROP(RedirectPropertyFixture,
                      RedirectMatchSingleCapture,
                      ())
{
    auto seg = *gen::pathSegment();

    std::string pattern = "^/old/([a-z0-9]+)$";
    std::string uri = "/old/" + seg;
    std::string tmpl = "https://example.com/new/$1";
    std::string expected = "https://example.com/new/" + seg;

    session_.set_request_uri(uri);

    auto *dir = make_redirect_match_dir(pattern, tmpl, 301);
    int rc = exec_redirect_match(session_.handle(), dir);
    RC_ASSERT(rc == 1);

    RC_ASSERT(session_.get_status_code() == 301);
    RC_ASSERT(session_.get_response_header("Location") == expected);

    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Property 16: Redirect 短路执行                                     */
/*                                                                     */
/*  After a matching Redirect, subsequent directives are not executed. */
/*  We simulate this by checking the return value (1 = short-circuit)  */
/*  and verifying that a Header set after the redirect is NOT applied  */
/*  when the caller respects the short-circuit return.                 */
/*                                                                     */
/*  **Validates: Requirement 7.5**                                     */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(RedirectPropertyFixture,
                      RedirectShortCircuitsPreventsSubsequent,
                      ())
{
    auto prefix = *gen::uriPath();
    auto target = *gen::targetUrl();
    auto status = *gen::redirectStatus();
    auto hdrName = *gen::headerName();
    auto hdrVal = *gen::headerValue();

    /* URI that matches the redirect prefix */
    auto suffix = *gen::pathSegment();
    std::string uri = prefix + "/" + suffix;
    session_.set_request_uri(uri);

    /* Build a directive chain: Redirect → Header set */
    auto *redirectDir = make_redirect_dir(prefix, target, status);
    auto *headerDir = make_header_dir(hdrName, hdrVal);
    redirectDir->next = headerDir;

    /* Simulate the module entry point: iterate directives, stop on redirect */
    htaccess_directive_t *d = redirectDir;
    bool short_circuited = false;
    while (d) {
        if (d->type == DIR_REDIRECT) {
            int rc = exec_redirect(session_.handle(), d);
            if (rc == 1) {
                short_circuited = true;
                break;
            }
        } else if (d->type == DIR_HEADER_SET) {
            exec_header(session_.handle(), d);
        }
        d = d->next;
    }

    /* Redirect should have triggered */
    RC_ASSERT(short_circuited);
    RC_ASSERT(session_.get_status_code() == status);
    std::string expected_location = target + "/" + suffix;
    RC_ASSERT(session_.get_response_header("Location") == expected_location);

    /* The Header set should NOT have been applied */
    RC_ASSERT(!session_.has_response_header(hdrName));

    /* Clean up — unlink before freeing to avoid double-free */
    redirectDir->next = nullptr;
    free_dir(redirectDir);
    free_dir(headerDir);
}

/* Also test short-circuit with RedirectMatch */
RC_GTEST_FIXTURE_PROP(RedirectPropertyFixture,
                      RedirectMatchShortCircuitsPreventsSubsequent,
                      ())
{
    auto seg = *gen::pathSegment();
    auto target = *gen::targetUrl();
    auto status = *gen::redirectStatus();
    auto hdrName = *gen::headerName();
    auto hdrVal = *gen::headerValue();

    std::string pattern = "^/redir/([a-z0-9]+)$";
    std::string uri = "/redir/" + seg;

    session_.set_request_uri(uri);

    auto *matchDir = make_redirect_match_dir(pattern, target, status);
    auto *headerDir = make_header_dir(hdrName, hdrVal);
    matchDir->next = headerDir;

    /* Simulate directive processing with short-circuit */
    htaccess_directive_t *d = matchDir;
    bool short_circuited = false;
    while (d) {
        if (d->type == DIR_REDIRECT_MATCH) {
            int rc = exec_redirect_match(session_.handle(), d);
            if (rc == 1) {
                short_circuited = true;
                break;
            }
        } else if (d->type == DIR_HEADER_SET) {
            exec_header(session_.handle(), d);
        }
        d = d->next;
    }

    RC_ASSERT(short_circuited);
    RC_ASSERT(!session_.has_response_header(hdrName));

    matchDir->next = nullptr;
    free_dir(matchDir);
    free_dir(headerDir);
}
