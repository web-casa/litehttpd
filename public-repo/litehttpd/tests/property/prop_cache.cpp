/**
 * prop_cache.cpp - Property-based tests for cache round-trip and mtime invalidation
 *
 * Feature: ols-htaccess-module
 *
 * Property 3: 缓存 Round-Trip
 *   For any filepath and directive list, cache_put(path, mtime, directives)
 *   followed by cache_get(path, mtime) should return the same directives pointer.
 *   Validates: Requirements 3.1, 3.2, 3.3
 *
 * Property 4: 缓存 mtime 失效
 *   For any cached entry (path, mtime1), cache_get(path, mtime2) where
 *   mtime2 != mtime1 should return -1 (miss).
 *   Validates: Requirements 3.4
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "htaccess_cache.h"
#include "htaccess_directive.h"
}

/* ------------------------------------------------------------------ */
/*  Generators                                                         */
/* ------------------------------------------------------------------ */

static const std::string kPathChars =
    "abcdefghijklmnopqrstuvwxyz0123456789_-";

/**
 * Generate a random file path like "/var/www/dir1/dir2/.htaccess".
 * Depth 1-4 directory components.
 */
static rc::Gen<std::string> genFilePath()
{
    return rc::gen::mapcat(
        rc::gen::inRange(1, 5),
        [](int depth) {
            return rc::gen::map(
                rc::gen::container<std::vector<std::string>>(
                    (std::size_t)depth,
                    rc::gen::mapcat(
                        rc::gen::inRange(1, 9),
                        [](int len) {
                            return rc::gen::map(
                                rc::gen::container<std::vector<char>>(
                                    (std::size_t)len,
                                    rc::gen::elementOf(kPathChars)),
                                [](const std::vector<char> &v) {
                                    return std::string(v.begin(), v.end());
                                });
                        })),
                [](const std::vector<std::string> &dirs) {
                    std::string path = "/var/www";
                    for (const auto &d : dirs)
                        path += "/" + d;
                    path += "/.htaccess";
                    return path;
                });
        });
}

/**
 * Generate a random mtime value (positive time_t).
 */
static rc::Gen<time_t> genMtime()
{
    return rc::gen::map(
        rc::gen::inRange(1, 2000000000),
        [](int v) -> time_t { return (time_t)v; });
}

/* ------------------------------------------------------------------ */
/*  Helper: build a simple directive list of Header set directives     */
/* ------------------------------------------------------------------ */

/**
 * Allocate a linked list of 1-5 "Header set" directives.
 * Caller takes ownership (cache_put will take ownership from caller).
 */
static htaccess_directive_t *make_directive_list(int count)
{
    htaccess_directive_t *head = nullptr;
    htaccess_directive_t *tail = nullptr;

    for (int i = 0; i < count; i++) {
        auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
        d->type = DIR_HEADER_SET;
        d->line_number = i + 1;

        /* Build unique name/value per directive */
        char namebuf[32], valbuf[32];
        snprintf(namebuf, sizeof(namebuf), "X-Test-%d", i);
        snprintf(valbuf, sizeof(valbuf), "value-%d", i);
        d->name = strdup(namebuf);
        d->value = strdup(valbuf);
        d->next = nullptr;

        if (!head) {
            head = d;
            tail = d;
        } else {
            tail->next = d;
            tail = d;
        }
    }
    return head;
}

/* ------------------------------------------------------------------ */
/*  Test fixture: init/destroy cache around each property invocation   */
/* ------------------------------------------------------------------ */

class CachePropertyFixture : public ::testing::Test {
public:
    void SetUp() override
    {
        htaccess_cache_init(64);
    }

    void TearDown() override
    {
        htaccess_cache_destroy();
    }
};

/* ------------------------------------------------------------------ */
/*  Property 3: 缓存 Round-Trip                                       */
/*                                                                     */
/*  For any filepath and directive list, cache_put(path, mtime, dirs)  */
/*  followed by cache_get(path, mtime) returns the same pointer.       */
/*                                                                     */
/*  **Validates: Requirements 3.1, 3.2, 3.3**                         */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(CachePropertyFixture,
                      CacheRoundTripReturnsSamePointer,
                      ())
{
    auto filepath = *genFilePath();
    auto mtime = *genMtime();
    auto numDirectives = *rc::gen::inRange(1, 6);

    /* Build a directive list (ownership goes to cache_put) */
    htaccess_directive_t *dirs = make_directive_list(numDirectives);
    RC_ASSERT(dirs != nullptr);

    /* Remember the pointer for comparison */
    htaccess_directive_t *original_ptr = dirs;

    /* Put into cache */
    int put_rc = htaccess_cache_put(filepath.c_str(), mtime, 0, 0, dirs);
    RC_ASSERT(put_rc == 0);

    /* Get from cache with same mtime */
    htaccess_directive_t *out = nullptr;
    int get_rc = htaccess_cache_get(filepath.c_str(), mtime, 0, 0, &out);
    RC_ASSERT(get_rc == 0);
    RC_ASSERT(out == original_ptr);

    /* Verify the directive list is intact: count matches */
    int count = 0;
    for (const htaccess_directive_t *d = out; d; d = d->next)
        count++;
    RC_ASSERT(count == numDirectives);
}

/* ------------------------------------------------------------------ */
/*  Property 4: 缓存 mtime 失效                                       */
/*                                                                     */
/*  For any cached entry (path, mtime1), cache_get(path, mtime2)       */
/*  where mtime2 != mtime1 should return -1 (miss).                    */
/*                                                                     */
/*  **Validates: Requirements 3.4**                                    */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(CachePropertyFixture,
                      CacheMtimeMismatchReturnsMiss,
                      ())
{
    auto filepath = *genFilePath();
    auto mtime1 = *genMtime();
    auto mtime2 = *genMtime();

    /* Ensure mtime1 != mtime2 */
    RC_PRE(mtime1 != mtime2);

    auto numDirectives = *rc::gen::inRange(1, 6);

    /* Build and cache a directive list */
    htaccess_directive_t *dirs = make_directive_list(numDirectives);
    RC_ASSERT(dirs != nullptr);

    int put_rc = htaccess_cache_put(filepath.c_str(), mtime1, 0, 0, dirs);
    RC_ASSERT(put_rc == 0);

    /* Attempt to get with different mtime — should miss */
    htaccess_directive_t *out = nullptr;
    int get_rc = htaccess_cache_get(filepath.c_str(), mtime2, 0, 0, &out);
    RC_ASSERT(get_rc == -1);
}
