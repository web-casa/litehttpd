/**
 * test_exec_expires_default.cpp - Unit tests for ExpiresDefault execution
 *
 * Tests ExpiresDefault as fallback when no ExpiresByType matches,
 * ExpiresByType precedence over ExpiresDefault, and ExpiresActive Off.
 *
 * Validates: Requirements 7.1-7.4
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <string>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_expires.h"
#include "htaccess_directive.h"
}

/* ---- Helpers ---- */

static htaccess_directive_t *make_expires_active(int active)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_EXPIRES_ACTIVE;
    d->line_number = 1;
    d->data.expires.active = active;
    return d;
}

static htaccess_directive_t *make_expires_by_type(const char *mime, long secs)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_EXPIRES_BY_TYPE;
    d->line_number = 2;
    d->name = strdup(mime);
    d->data.expires.duration_sec = secs;
    return d;
}

static htaccess_directive_t *make_expires_default(long secs)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_EXPIRES_DEFAULT;
    d->line_number = 3;
    d->data.expires.duration_sec = secs;
    return d;
}

static void free_dir_list(htaccess_directive_t *head)
{
    while (head) {
        htaccess_directive_t *next = head->next;
        free(head->name);
        free(head->value);
        free(head);
        head = next;
    }
}

/* ---- Test fixture ---- */

class ExpiresDefaultExecTest : public ::testing::Test {
public:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
protected:
    MockSession session_;
};

/* ExpiresDefault used when no ExpiresByType matches */
TEST_F(ExpiresDefaultExecTest, FallbackWhenNoByTypeMatch)
{
    auto *active = make_expires_active(1);
    auto *bytype = make_expires_by_type("text/html", 3600);
    auto *def = make_expires_default(2592000);
    active->next = bytype;
    bytype->next = def;

    int rc = exec_expires(session_.handle(), active, "image/png");
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_TRUE(session_.has_response_header("Cache-Control"));
    EXPECT_EQ(session_.get_response_header("Cache-Control"), "max-age=2592000");
    EXPECT_TRUE(session_.has_response_header("Expires"));

    free_dir_list(active);
}

/* ExpiresByType takes precedence over ExpiresDefault */
TEST_F(ExpiresDefaultExecTest, ByTypeTakesPrecedence)
{
    auto *active = make_expires_active(1);
    auto *bytype = make_expires_by_type("text/html", 3600);
    auto *def = make_expires_default(2592000);
    active->next = bytype;
    bytype->next = def;

    int rc = exec_expires(session_.handle(), active, "text/html");
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_TRUE(session_.has_response_header("Cache-Control"));
    EXPECT_EQ(session_.get_response_header("Cache-Control"), "max-age=3600");

    free_dir_list(active);
}

/* ExpiresDefault alone (no ExpiresByType) when active */
TEST_F(ExpiresDefaultExecTest, DefaultAloneWhenActive)
{
    auto *active = make_expires_active(1);
    auto *def = make_expires_default(86400);
    active->next = def;

    int rc = exec_expires(session_.handle(), active, "application/json");
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_TRUE(session_.has_response_header("Cache-Control"));
    EXPECT_EQ(session_.get_response_header("Cache-Control"), "max-age=86400");

    free_dir_list(active);
}

/* ExpiresDefault not used when ExpiresActive is Off */
TEST_F(ExpiresDefaultExecTest, DefaultNotUsedWhenInactive)
{
    auto *active = make_expires_active(0);
    auto *def = make_expires_default(86400);
    active->next = def;

    int rc = exec_expires(session_.handle(), active, "application/json");
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_FALSE(session_.has_response_header("Cache-Control"));
    EXPECT_FALSE(session_.has_response_header("Expires"));

    free_dir_list(active);
}
