/**
 * test_exec_redirect.cpp - Unit tests for Redirect/RedirectMatch executors
 *
 * Tests default 302, specified status codes, regex matching, backreferences,
 * non-matching URIs, and edge cases.
 *
 * Validates: Requirements 7.1, 7.2, 7.3, 7.4, 7.5
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_redirect.h"
#include "htaccess_exec_header.h"
#include "htaccess_directive.h"
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_redirect(const char *prefix,
                                           const char *target,
                                           int status_code)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_REDIRECT;
    d->line_number = 1;
    d->name = prefix ? strdup(prefix) : nullptr;
    d->value = target ? strdup(target) : nullptr;
    d->data.redirect.status_code = status_code;
    d->data.redirect.pattern = nullptr;
    d->next = nullptr;
    return d;
}

static htaccess_directive_t *make_redirect_match(const char *pattern,
                                                 const char *target,
                                                 int status_code)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_REDIRECT_MATCH;
    d->line_number = 1;
    d->name = nullptr;
    d->value = target ? strdup(target) : nullptr;
    d->data.redirect.status_code = status_code;
    d->data.redirect.pattern = pattern ? strdup(pattern) : nullptr;
    d->next = nullptr;
    return d;
}

static htaccess_directive_t *make_header_set(const char *name, const char *value)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_HEADER_SET;
    d->line_number = 2;
    d->name = name ? strdup(name) : nullptr;
    d->value = value ? strdup(value) : nullptr;
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

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class ExecRedirectTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }

    MockSession session_;
};

/* ------------------------------------------------------------------ */
/*  Redirect: default 302                                              */
/* ------------------------------------------------------------------ */

TEST_F(ExecRedirectTest, RedirectDefault302)
{
    /* /old/page → /new + tail "/page" = /new/page */
    session_.set_request_uri("/old/page");
    auto *dir = make_redirect("/old", "https://example.com/new", 0);
    EXPECT_EQ(exec_redirect(session_.handle(), dir), 1);
    EXPECT_EQ(session_.get_status_code(), 302);
    EXPECT_EQ(session_.get_response_header("Location"), "https://example.com/new/page");
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Redirect: specified status codes                                   */
/* ------------------------------------------------------------------ */

TEST_F(ExecRedirectTest, Redirect301)
{
    session_.set_request_uri("/old/page");
    auto *dir = make_redirect("/old", "https://example.com/new", 301);
    EXPECT_EQ(exec_redirect(session_.handle(), dir), 1);
    EXPECT_EQ(session_.get_status_code(), 301);
    EXPECT_EQ(session_.get_response_header("Location"), "https://example.com/new/page");
    free_dir(dir);
}

TEST_F(ExecRedirectTest, Redirect307)
{
    session_.set_request_uri("/temp/resource");
    auto *dir = make_redirect("/temp", "https://example.com/moved", 307);
    EXPECT_EQ(exec_redirect(session_.handle(), dir), 1);
    EXPECT_EQ(session_.get_status_code(), 307);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Redirect: no match                                                 */
/* ------------------------------------------------------------------ */

TEST_F(ExecRedirectTest, RedirectNoMatch)
{
    session_.set_request_uri("/other/page");
    auto *dir = make_redirect("/old", "https://example.com/new", 302);
    EXPECT_EQ(exec_redirect(session_.handle(), dir), 0);
    /* Status should remain unchanged */
    EXPECT_EQ(session_.get_status_code(), 200);
    EXPECT_FALSE(session_.has_response_header("Location"));
    free_dir(dir);
}

TEST_F(ExecRedirectTest, RedirectExactPrefixMatch)
{
    /* URI exactly equals the prefix */
    session_.set_request_uri("/old");
    auto *dir = make_redirect("/old", "https://example.com/new", 302);
    EXPECT_EQ(exec_redirect(session_.handle(), dir), 1);
    EXPECT_EQ(session_.get_status_code(), 302);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  RedirectMatch: basic regex                                         */
/* ------------------------------------------------------------------ */

TEST_F(ExecRedirectTest, RedirectMatchBasic)
{
    session_.set_request_uri("/blog/2024/hello");
    auto *dir = make_redirect_match("^/blog/", "https://example.com/articles/", 301);
    EXPECT_EQ(exec_redirect_match(session_.handle(), dir), 1);
    EXPECT_EQ(session_.get_status_code(), 301);
    EXPECT_EQ(session_.get_response_header("Location"), "https://example.com/articles/");
    free_dir(dir);
}

TEST_F(ExecRedirectTest, RedirectMatchNoMatch)
{
    session_.set_request_uri("/about");
    auto *dir = make_redirect_match("^/blog/", "https://example.com/articles/", 301);
    EXPECT_EQ(exec_redirect_match(session_.handle(), dir), 0);
    EXPECT_FALSE(session_.has_response_header("Location"));
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  RedirectMatch: backreferences                                      */
/* ------------------------------------------------------------------ */

TEST_F(ExecRedirectTest, RedirectMatchSingleBackref)
{
    session_.set_request_uri("/old/page123");
    auto *dir = make_redirect_match("^/old/(.+)$",
                                    "https://example.com/new/$1", 302);
    EXPECT_EQ(exec_redirect_match(session_.handle(), dir), 1);
    EXPECT_EQ(session_.get_response_header("Location"),
              "https://example.com/new/page123");
    free_dir(dir);
}

TEST_F(ExecRedirectTest, RedirectMatchMultipleBackrefs)
{
    session_.set_request_uri("/blog/2024/hello-world");
    auto *dir = make_redirect_match("^/blog/([0-9]+)/(.+)$",
                                    "https://example.com/posts/$1/$2", 301);
    EXPECT_EQ(exec_redirect_match(session_.handle(), dir), 1);
    EXPECT_EQ(session_.get_response_header("Location"),
              "https://example.com/posts/2024/hello-world");
    free_dir(dir);
}

TEST_F(ExecRedirectTest, RedirectMatchBackrefReorder)
{
    session_.set_request_uri("/a/b");
    auto *dir = make_redirect_match("^/([a-z]+)/([a-z]+)$",
                                    "https://example.com/$2/$1", 302);
    EXPECT_EQ(exec_redirect_match(session_.handle(), dir), 1);
    EXPECT_EQ(session_.get_response_header("Location"),
              "https://example.com/b/a");
    free_dir(dir);
}

TEST_F(ExecRedirectTest, RedirectMatchDefault302)
{
    session_.set_request_uri("/test/path");
    auto *dir = make_redirect_match("^/test/", "https://example.com/dest", 0);
    EXPECT_EQ(exec_redirect_match(session_.handle(), dir), 1);
    EXPECT_EQ(session_.get_status_code(), 302);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  Short-circuit behavior                                             */
/* ------------------------------------------------------------------ */

TEST_F(ExecRedirectTest, ShortCircuitStopsSubsequentDirectives)
{
    session_.set_request_uri("/old/page");

    auto *redir = make_redirect("/old", "https://example.com/new", 301);
    auto *hdr = make_header_set("X-After-Redirect", "should-not-appear");
    redir->next = hdr;

    /* Simulate module processing loop */
    htaccess_directive_t *d = redir;
    bool stopped = false;
    while (d) {
        if (d->type == DIR_REDIRECT) {
            if (exec_redirect(session_.handle(), d) == 1) {
                stopped = true;
                break;
            }
        } else if (d->type == DIR_HEADER_SET) {
            exec_header(session_.handle(), d);
        }
        d = d->next;
    }

    EXPECT_TRUE(stopped);
    EXPECT_EQ(session_.get_status_code(), 301);
    EXPECT_FALSE(session_.has_response_header("X-After-Redirect"));

    redir->next = nullptr;
    free_dir(redir);
    free_dir(hdr);
}

/* ------------------------------------------------------------------ */
/*  Edge cases                                                         */
/* ------------------------------------------------------------------ */

TEST_F(ExecRedirectTest, NullSessionReturnsError)
{
    auto *dir = make_redirect("/old", "https://example.com", 302);
    EXPECT_EQ(exec_redirect(nullptr, dir), -1);
    free_dir(dir);
}

TEST_F(ExecRedirectTest, NullDirectiveReturnsError)
{
    EXPECT_EQ(exec_redirect(session_.handle(), nullptr), -1);
    EXPECT_EQ(exec_redirect_match(session_.handle(), nullptr), -1);
}

TEST_F(ExecRedirectTest, NullNameReturnsError)
{
    auto *dir = make_redirect(nullptr, "https://example.com", 302);
    EXPECT_EQ(exec_redirect(session_.handle(), dir), -1);
    free_dir(dir);
}

TEST_F(ExecRedirectTest, NullValueReturnsError)
{
    auto *dir = make_redirect("/old", nullptr, 302);
    EXPECT_EQ(exec_redirect(session_.handle(), dir), -1);
    free_dir(dir);
}

TEST_F(ExecRedirectTest, NullPatternReturnsError)
{
    auto *dir = make_redirect_match(nullptr, "https://example.com", 302);
    EXPECT_EQ(exec_redirect_match(session_.handle(), dir), -1);
    free_dir(dir);
}

TEST_F(ExecRedirectTest, InvalidRegexReturnsError)
{
    session_.set_request_uri("/test");
    auto *dir = make_redirect_match("[invalid", "https://example.com", 302);
    EXPECT_EQ(exec_redirect_match(session_.handle(), dir), -1);
    free_dir(dir);
}

TEST_F(ExecRedirectTest, WrongTypeReturnsError)
{
    session_.set_request_uri("/test");
    auto *dir = make_redirect("/test", "https://example.com", 302);
    dir->type = DIR_HEADER_SET; /* Wrong type */
    EXPECT_EQ(exec_redirect(session_.handle(), dir), -1);
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  410 Gone (no Location header)                                      */
/* ------------------------------------------------------------------ */

TEST_F(ExecRedirectTest, RedirectGone410NoLocation)
{
    session_.set_request_uri("/removed/page");
    auto *dir = make_redirect("/removed", nullptr, 410);
    EXPECT_EQ(exec_redirect(session_.handle(), dir), 1);
    EXPECT_EQ(session_.get_status_code(), 410);
    EXPECT_FALSE(session_.has_response_header("Location"));
    free_dir(dir);
}

TEST_F(ExecRedirectTest, RedirectGone410NullValueOk)
{
    /* NULL value with 410 should NOT return error */
    session_.set_request_uri("/old");
    auto *dir = make_redirect("/old", nullptr, 410);
    EXPECT_NE(exec_redirect(session_.handle(), dir), -1);
    free_dir(dir);
}

TEST_F(ExecRedirectTest, RedirectNon410NullValueStillError)
{
    /* NULL value with non-410 should still return error */
    auto *dir = make_redirect("/old", nullptr, 301);
    EXPECT_EQ(exec_redirect(session_.handle(), dir), -1);
    free_dir(dir);
}

TEST_F(ExecRedirectTest, RedirectMatchGone410)
{
    session_.set_request_uri("/removed/page");
    auto *dir = make_redirect_match("^/removed/", nullptr, 410);
    EXPECT_EQ(exec_redirect_match(session_.handle(), dir), 1);
    EXPECT_EQ(session_.get_status_code(), 410);
    EXPECT_FALSE(session_.has_response_header("Location"));
    free_dir(dir);
}

TEST_F(ExecRedirectTest, RedirectMatchNon410NullValueStillError)
{
    session_.set_request_uri("/test");
    auto *dir = make_redirect_match("^/test", nullptr, 301);
    EXPECT_EQ(exec_redirect_match(session_.handle(), dir), -1);
    free_dir(dir);
}
