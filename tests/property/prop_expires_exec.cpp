/**
 * prop_expires_exec.cpp - Property-based test for ExpiresByType header setting
 *
 * Feature: ols-htaccess-module, Property 21: ExpiresByType 头设置
 *
 * Validates: Requirement 10.3
 *
 * Property: For any MIME type and expiration duration, when ExpiresActive
 * is On, executing ExpiresByType sets the correct Cache-Control: max-age=N
 * header and an Expires header on the response.
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

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_expires_active(int active)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_EXPIRES_ACTIVE;
    d->line_number = 1;
    d->name = nullptr;
    d->value = nullptr;
    d->data.expires.active = active;
    d->next = nullptr;
    return d;
}

static htaccess_directive_t *make_expires_by_type(const std::string &mime,
                                                   long duration_sec)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_EXPIRES_BY_TYPE;
    d->line_number = 2;
    d->name = strdup(mime.c_str());
    d->value = nullptr;
    d->data.expires.duration_sec = duration_sec;
    d->next = nullptr;
    return d;
}

static void free_directives(htaccess_directive_t *head)
{
    while (head) {
        htaccess_directive_t *next = head->next;
        free(head->name);
        free(head->value);
        free(head);
        head = next;
    }
}

/** Generate a simple MIME type string like "text/html", "image/png", etc. */
static rc::Gen<std::string> genMimeType()
{
    return rc::gen::element<std::string>(
        "text/html", "text/css", "text/javascript",
        "image/png", "image/jpeg", "image/gif",
        "application/json", "application/xml",
        "application/pdf", "font/woff2"
    );
}

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class ExpiresExecPropertyFixture : public ::testing::Test {
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
/*  Property 21: ExpiresByType 头设置                                  */
/*                                                                     */
/*  When ExpiresActive is On and ExpiresByType matches the content     */
/*  type, the response includes Cache-Control: max-age=N and an        */
/*  Expires header.                                                    */
/*                                                                     */
/*  **Validates: Requirement 10.3**                                    */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(ExpiresExecPropertyFixture,
                      ExpiresByTypeSetsCorrectHeaders,
                      ())
{
    auto mime = *genMimeType();
    auto expiresResult = *gen::expiresDuration();
    long duration_sec = expiresResult.second;

    /* Build directive list: ExpiresActive On -> ExpiresByType mime duration */
    htaccess_directive_t *active_dir = make_expires_active(1);
    htaccess_directive_t *type_dir = make_expires_by_type(mime, duration_sec);
    active_dir->next = type_dir;

    int rc = exec_expires(session_.handle(), active_dir, mime.c_str());
    RC_ASSERT(rc == LSI_OK);

    /* Verify Cache-Control header is set to max-age=N */
    RC_ASSERT(session_.has_response_header("Cache-Control"));
    std::string cc = session_.get_response_header("Cache-Control");
    std::string expected_cc = "max-age=" + std::to_string(duration_sec);
    RC_ASSERT(cc == expected_cc);

    /* Verify Expires header is present (non-empty) */
    RC_ASSERT(session_.has_response_header("Expires"));
    std::string expires = session_.get_response_header("Expires");
    RC_ASSERT(!expires.empty());

    free_directives(active_dir);
}

/* ------------------------------------------------------------------ */
/*  Additional property: ExpiresActive Off suppresses headers          */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(ExpiresExecPropertyFixture,
                      ExpiresActiveOffSuppressesHeaders,
                      ())
{
    auto mime = *genMimeType();
    auto expiresResult = *gen::expiresDuration();
    long duration_sec = expiresResult.second;

    /* Build directive list: ExpiresActive Off -> ExpiresByType */
    htaccess_directive_t *active_dir = make_expires_active(0);
    htaccess_directive_t *type_dir = make_expires_by_type(mime, duration_sec);
    active_dir->next = type_dir;

    int rc = exec_expires(session_.handle(), active_dir, mime.c_str());
    RC_ASSERT(rc == LSI_OK);

    /* No headers should be set when ExpiresActive is Off */
    RC_ASSERT(!session_.has_response_header("Cache-Control"));
    RC_ASSERT(!session_.has_response_header("Expires"));

    free_directives(active_dir);
}

/* ------------------------------------------------------------------ */
/*  Additional property: Non-matching MIME type sets no headers        */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(ExpiresExecPropertyFixture,
                      NonMatchingMimeTypeSetsNoHeaders,
                      ())
{
    auto expiresResult = *gen::expiresDuration();
    long duration_sec = expiresResult.second;

    /* Build directive list: ExpiresActive On -> ExpiresByType for text/html */
    htaccess_directive_t *active_dir = make_expires_active(1);
    htaccess_directive_t *type_dir = make_expires_by_type("text/html", duration_sec);
    active_dir->next = type_dir;

    /* Request with a different content type */
    int rc = exec_expires(session_.handle(), active_dir, "image/png");
    RC_ASSERT(rc == LSI_OK);

    /* No headers should be set for non-matching MIME type */
    RC_ASSERT(!session_.has_response_header("Cache-Control"));
    RC_ASSERT(!session_.has_response_header("Expires"));

    free_directives(active_dir);
}
