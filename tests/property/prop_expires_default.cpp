/**
 * prop_expires_default.cpp - Property-based test for ExpiresDefault fallback
 *
 * Feature: htaccess-v2-enhancements, Property 32: ExpiresDefault 回退
 *
 * Validates: Requirements 7.2, 7.3
 *
 * Property: For any MIME type, when ExpiresActive is On:
 *   - If no ExpiresByType matches, ExpiresDefault sets Cache-Control/Expires
 *   - If ExpiresByType matches, it takes precedence over ExpiresDefault
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <cstring>
#include <cstdlib>

#include "mock_lsiapi.h"
#include "gen_expires.h"

extern "C" {
#include "htaccess_exec_expires.h"
#include "htaccess_directive.h"
}

/* ---- Helpers ---- */

static htaccess_directive_t *make_active(int on)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_EXPIRES_ACTIVE;
    d->data.expires.active = on;
    return d;
}

static htaccess_directive_t *make_by_type(const char *mime, long secs)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_EXPIRES_BY_TYPE;
    d->name = strdup(mime);
    d->data.expires.duration_sec = secs;
    return d;
}

static htaccess_directive_t *make_default(long secs)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_EXPIRES_DEFAULT;
    d->data.expires.duration_sec = secs;
    return d;
}

static void free_dirs(htaccess_directive_t *head)
{
    while (head) {
        htaccess_directive_t *n = head->next;
        free(head->name);
        free(head->value);
        free(head);
        head = n;
    }
}

static rc::Gen<std::string> genMimeType()
{
    return rc::gen::element<std::string>(
        "text/html", "text/css", "text/javascript",
        "image/png", "image/jpeg", "image/gif",
        "application/json", "application/xml",
        "application/pdf", "font/woff2"
    );
}

/* ---- Fixture ---- */

class ExpiresDefaultPropFixture : public ::testing::Test {
public:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
protected:
    MockSession session_;
};

/* Property 32a: ExpiresDefault used when no ExpiresByType matches */
RC_GTEST_FIXTURE_PROP(ExpiresDefaultPropFixture,
                      FallbackUsedWhenNoByTypeMatch,
                      ())
{
    auto bytype_mime = *genMimeType();
    auto request_mime = *genMimeType();
    RC_PRE(bytype_mime != request_mime);

    auto bytype_dur = *gen::expiresDuration();
    auto default_dur = *gen::expiresDuration();

    auto *active = make_active(1);
    auto *bt = make_by_type(bytype_mime.c_str(), bytype_dur.second);
    auto *def = make_default(default_dur.second);
    active->next = bt;
    bt->next = def;

    int rc = exec_expires(session_.handle(), active, request_mime.c_str());
    RC_ASSERT(rc == LSI_OK);

    /* ExpiresDefault should have been used */
    RC_ASSERT(session_.has_response_header("Cache-Control"));
    std::string expected = "max-age=" + std::to_string(default_dur.second);
    RC_ASSERT(session_.get_response_header("Cache-Control") == expected);
    RC_ASSERT(session_.has_response_header("Expires"));

    free_dirs(active);
}

/* Property 32b: ExpiresByType takes precedence over ExpiresDefault */
RC_GTEST_FIXTURE_PROP(ExpiresDefaultPropFixture,
                      ByTypeTakesPrecedenceOverDefault,
                      ())
{
    auto mime = *genMimeType();
    auto bytype_dur = *gen::expiresDuration();
    auto default_dur = *gen::expiresDuration();
    RC_PRE(bytype_dur.second != default_dur.second);

    auto *active = make_active(1);
    auto *bt = make_by_type(mime.c_str(), bytype_dur.second);
    auto *def = make_default(default_dur.second);
    active->next = bt;
    bt->next = def;

    int rc = exec_expires(session_.handle(), active, mime.c_str());
    RC_ASSERT(rc == LSI_OK);

    /* ExpiresByType value should be used, not ExpiresDefault */
    RC_ASSERT(session_.has_response_header("Cache-Control"));
    std::string expected = "max-age=" + std::to_string(bytype_dur.second);
    RC_ASSERT(session_.get_response_header("Cache-Control") == expected);

    free_dirs(active);
}

/* Property 32c: ExpiresDefault alone (no ExpiresByType) sets headers */
RC_GTEST_FIXTURE_PROP(ExpiresDefaultPropFixture,
                      DefaultAloneSetsHeaders,
                      ())
{
    auto mime = *genMimeType();
    auto default_dur = *gen::expiresDuration();

    auto *active = make_active(1);
    auto *def = make_default(default_dur.second);
    active->next = def;

    int rc = exec_expires(session_.handle(), active, mime.c_str());
    RC_ASSERT(rc == LSI_OK);

    RC_ASSERT(session_.has_response_header("Cache-Control"));
    std::string expected = "max-age=" + std::to_string(default_dur.second);
    RC_ASSERT(session_.get_response_header("Cache-Control") == expected);
    RC_ASSERT(session_.has_response_header("Expires"));

    free_dirs(active);
}

/* Property 32d: ExpiresActive Off suppresses ExpiresDefault */
RC_GTEST_FIXTURE_PROP(ExpiresDefaultPropFixture,
                      InactiveSuppressesDefault,
                      ())
{
    auto mime = *genMimeType();
    auto default_dur = *gen::expiresDuration();

    auto *active = make_active(0);
    auto *def = make_default(default_dur.second);
    active->next = def;

    int rc = exec_expires(session_.handle(), active, mime.c_str());
    RC_ASSERT(rc == LSI_OK);

    RC_ASSERT(!session_.has_response_header("Cache-Control"));
    RC_ASSERT(!session_.has_response_header("Expires"));

    free_dirs(active);
}
