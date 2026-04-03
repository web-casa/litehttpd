/**
 * test_cache.cpp - Unit tests for htaccess_cache hash table cache
 *
 * Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5, 3.6
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "htaccess_cache.h"
#include "htaccess_directive.h"
}

/* ---- Helper: allocate a directive with strdup'd name/value ---- */

static htaccess_directive_t *make_directive(directive_type_t type,
                                            const char *name,
                                            const char *value,
                                            int line)
{
    auto *d = static_cast<htaccess_directive_t *>(
        calloc(1, sizeof(htaccess_directive_t)));
    d->type = type;
    d->line_number = line;
    if (name)  d->name  = strdup(name);
    if (value) d->value = strdup(value);
    d->next = nullptr;
    return d;
}

/* ==================================================================
 *  Test fixture: init/destroy cache around each test
 * ================================================================== */

class CacheTest : public ::testing::Test {
protected:
    void SetUp() override    { ASSERT_EQ(htaccess_cache_init(16), 0); }
    void TearDown() override { htaccess_cache_destroy(); }
};

/* ==================================================================
 *  1. Init and destroy (basic lifecycle)
 * ================================================================== */

TEST(CacheLifecycle, InitAndDestroySucceeds)
{
    ASSERT_EQ(htaccess_cache_init(32), 0);
    htaccess_cache_destroy();
}

TEST(CacheLifecycle, InitZeroBucketsUsesDefault)
{
    /* Passing 0 should fall back to 64 buckets internally */
    ASSERT_EQ(htaccess_cache_init(0), 0);
    htaccess_cache_destroy();
}

/* ==================================================================
 *  2. Put then get with matching mtime → hit
 * ================================================================== */

TEST_F(CacheTest, PutThenGetMatchingMtime_ReturnsHit)
{
    htaccess_directive_t *dirs = make_directive(
        DIR_HEADER_SET, "X-Test", "value1", 1);
    time_t mtime = 1000;

    ASSERT_EQ(htaccess_cache_put("/var/www/.htaccess", mtime, 0, 0, dirs), 0);

    htaccess_directive_t *out = nullptr;
    ASSERT_EQ(htaccess_cache_get("/var/www/.htaccess", mtime, 0, 0, &out), 0);
    EXPECT_EQ(out, dirs);
    EXPECT_STREQ(out->name, "X-Test");
    EXPECT_STREQ(out->value, "value1");
}

/* ==================================================================
 *  3. Get with non-matching mtime → miss
 * ================================================================== */

TEST_F(CacheTest, GetWithDifferentMtime_ReturnsMiss)
{
    htaccess_directive_t *dirs = make_directive(
        DIR_HEADER_SET, "X-Test", "value1", 1);

    ASSERT_EQ(htaccess_cache_put("/var/www/.htaccess", 1000, 0, 0, dirs), 0);

    htaccess_directive_t *out = nullptr;
    EXPECT_EQ(htaccess_cache_get("/var/www/.htaccess", 2000, 0, 0, &out), -1);
}

/* ==================================================================
 *  4. Get for non-existent path → miss
 * ================================================================== */

TEST_F(CacheTest, GetNonExistentPath_ReturnsMiss)
{
    htaccess_directive_t *out = nullptr;
    EXPECT_EQ(htaccess_cache_get("/no/such/path/.htaccess", 1000, 0, 0, &out), -1);
}

/* ==================================================================
 *  5. Put replaces existing entry (same path, new mtime)
 * ================================================================== */

TEST_F(CacheTest, PutReplacesExistingEntry)
{
    htaccess_directive_t *dirs1 = make_directive(
        DIR_HEADER_SET, "X-Old", "old-val", 1);
    htaccess_directive_t *dirs2 = make_directive(
        DIR_HEADER_SET, "X-New", "new-val", 2);

    const char *path = "/var/www/.htaccess";

    ASSERT_EQ(htaccess_cache_put(path, 1000, 0, 0, dirs1), 0);
    ASSERT_EQ(htaccess_cache_put(path, 2000, 0, 0, dirs2), 0);

    /* Old mtime should miss */
    htaccess_directive_t *out = nullptr;
    EXPECT_EQ(htaccess_cache_get(path, 1000, 0, 0, &out), -1);

    /* New mtime should hit with new directives */
    ASSERT_EQ(htaccess_cache_get(path, 2000, 0, 0, &out), 0);
    EXPECT_EQ(out, dirs2);
    EXPECT_STREQ(out->name, "X-New");
    EXPECT_STREQ(out->value, "new-val");
}

/* ==================================================================
 *  6. Multiple entries in same cache
 * ================================================================== */

TEST_F(CacheTest, MultipleEntriesCoexist)
{
    htaccess_directive_t *dirs_a = make_directive(
        DIR_HEADER_SET, "X-A", "a-val", 1);
    htaccess_directive_t *dirs_b = make_directive(
        DIR_PHP_VALUE, "upload_max", "64M", 2);
    htaccess_directive_t *dirs_c = make_directive(
        DIR_SETENV, "APP_ENV", "production", 3);

    ASSERT_EQ(htaccess_cache_put("/site-a/.htaccess", 100, 0, 0, dirs_a), 0);
    ASSERT_EQ(htaccess_cache_put("/site-b/.htaccess", 200, 0, 0, dirs_b), 0);
    ASSERT_EQ(htaccess_cache_put("/site-c/.htaccess", 300, 0, 0, dirs_c), 0);

    htaccess_directive_t *out = nullptr;

    ASSERT_EQ(htaccess_cache_get("/site-a/.htaccess", 100, 0, 0, &out), 0);
    EXPECT_STREQ(out->name, "X-A");

    ASSERT_EQ(htaccess_cache_get("/site-b/.htaccess", 200, 0, 0, &out), 0);
    EXPECT_STREQ(out->name, "upload_max");

    ASSERT_EQ(htaccess_cache_get("/site-c/.htaccess", 300, 0, 0, &out), 0);
    EXPECT_STREQ(out->name, "APP_ENV");
}

/* ==================================================================
 *  7. Destroy cleans up all entries
 * ================================================================== */

TEST(CacheDestroyCleanup, DestroyFreesAllEntries)
{
    ASSERT_EQ(htaccess_cache_init(8), 0);

    for (int i = 0; i < 10; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dir%d/.htaccess", i);
        char name[32], val[32];
        snprintf(name, sizeof(name), "H%d", i);
        snprintf(val, sizeof(val), "V%d", i);
        htaccess_directive_t *d = make_directive(DIR_HEADER_SET, name, val, i);
        ASSERT_EQ(htaccess_cache_put(path, (time_t)(i + 1), 0, 0, d), 0);
    }

    /* Destroy should free everything without crashing */
    htaccess_cache_destroy();

    /* After destroy, get should fail (no cache) */
    htaccess_directive_t *out = nullptr;
    EXPECT_EQ(htaccess_cache_get("/dir0/.htaccess", 1, 0, 0, &out), -1);
}

/* ==================================================================
 *  8. Null parameter handling
 * ================================================================== */

TEST_F(CacheTest, GetNullFilepath_ReturnsMiss)
{
    htaccess_directive_t *out = nullptr;
    EXPECT_EQ(htaccess_cache_get(nullptr, 1000, 0, 0, &out), -1);
}

TEST_F(CacheTest, GetNullOutDirectives_ReturnsMiss)
{
    EXPECT_EQ(htaccess_cache_get("/some/path", 1000, 0, 0, nullptr), -1);
}

TEST_F(CacheTest, PutNullFilepath_ReturnsError)
{
    htaccess_directive_t *dirs = make_directive(
        DIR_HEADER_SET, "X-Test", "val", 1);
    EXPECT_EQ(htaccess_cache_put(nullptr, 1000, 0, 0, dirs), -1);
    /* We must free dirs ourselves since put didn't take ownership */
    htaccess_directives_free(dirs);
}

TEST_F(CacheTest, PutNullDirectives_Succeeds)
{
    /* Storing NULL directives is valid (empty .htaccess) */
    EXPECT_EQ(htaccess_cache_put("/empty/.htaccess", 1000, 0, 0, nullptr), 0);

    htaccess_directive_t *out = nullptr;
    ASSERT_EQ(htaccess_cache_get("/empty/.htaccess", 1000, 0, 0, &out), 0);
    EXPECT_EQ(out, nullptr);
}

/* ==================================================================
 *  9. Double destroy is safe
 * ================================================================== */

TEST(CacheDoubleDestroy, DoubleDestroyIsSafe)
{
    ASSERT_EQ(htaccess_cache_init(8), 0);
    htaccess_cache_destroy();
    /* Second destroy should be a no-op, not crash */
    htaccess_cache_destroy();
}

TEST(CacheDoubleDestroy, DestroyWithoutInitIsSafe)
{
    /* Calling destroy when init was never called should not crash */
    htaccess_cache_destroy();
}
