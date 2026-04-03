/**
 * test_dirwalker.cpp - Unit tests for DirWalker directory hierarchy traversal
 *
 * Tests single-level, multi-level, and empty directory traversal,
 * as well as directive override behavior.
 *
 * Validates: Requirements 13.1, 13.2, 13.3
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "htaccess_cache.h"
#include "htaccess_directive.h"
#include "htaccess_dirwalker.h"
}

/* ---- Helpers ---- */

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

static void append_directive(htaccess_directive_t **head,
                             htaccess_directive_t *node)
{
    if (!*head) {
        *head = node;
        return;
    }
    htaccess_directive_t *tail = *head;
    while (tail->next)
        tail = tail->next;
    tail->next = node;
}

static int count_directives(const htaccess_directive_t *head)
{
    int n = 0;
    for (const htaccess_directive_t *d = head; d; d = d->next)
        n++;
    return n;
}

static const htaccess_directive_t *find_by_type_and_name(
    const htaccess_directive_t *head,
    directive_type_t type,
    const char *name)
{
    for (const htaccess_directive_t *d = head; d; d = d->next) {
        if (d->type == type) {
            if (!name || (d->name && strcmp(d->name, name) == 0))
                return d;
        }
    }
    return nullptr;
}

/* ==================================================================
 *  Test fixture: init/destroy cache around each test
 * ================================================================== */

class DirWalkerTest : public ::testing::Test {
protected:
    void SetUp() override    { ASSERT_EQ(htaccess_cache_init(16), 0); }
    void TearDown() override { htaccess_cache_destroy(); }
};

/* ==================================================================
 *  1. Single-level: doc_root only
 * ================================================================== */

TEST_F(DirWalkerTest, SingleLevel_DocRootOnly)
{
    /* Place directives at doc_root */
    htaccess_directive_t *dirs = make_directive(
        DIR_HEADER_SET, "X-Frame-Options", "DENY", 1);

    htaccess_cache_put("/var/www/html/.htaccess", 0, 0, 0, dirs);

    htaccess_directive_t *merged = htaccess_dirwalk(
        nullptr, "/var/www/html", "/var/www/html");

    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(count_directives(merged), 1);
    EXPECT_STREQ(merged->name, "X-Frame-Options");
    EXPECT_STREQ(merged->value, "DENY");

    htaccess_directives_free(merged);
}

/* ==================================================================
 *  2. Multi-level: root + subdirectory, no overlap
 * ================================================================== */

TEST_F(DirWalkerTest, MultiLevel_NoOverlap)
{
    /* Root: Header set X-Root root-val */
    htaccess_directive_t *root_dirs = make_directive(
        DIR_HEADER_SET, "X-Root", "root-val", 1);
    htaccess_cache_put("/var/www/html/.htaccess", 0, 0, 0, root_dirs);

    /* Child: SetEnv APP_ENV production */
    htaccess_directive_t *child_dirs = make_directive(
        DIR_SETENV, "APP_ENV", "production", 1);
    htaccess_cache_put("/var/www/html/app/.htaccess", 0, 0, 0, child_dirs);

    htaccess_directive_t *merged = htaccess_dirwalk(
        nullptr, "/var/www/html", "/var/www/html/app");

    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(count_directives(merged), 2);

    auto *header = find_by_type_and_name(merged, DIR_HEADER_SET, "X-Root");
    ASSERT_NE(header, nullptr);
    EXPECT_STREQ(header->value, "root-val");

    auto *env = find_by_type_and_name(merged, DIR_SETENV, "APP_ENV");
    ASSERT_NE(env, nullptr);
    EXPECT_STREQ(env->value, "production");

    htaccess_directives_free(merged);
}

/* ==================================================================
 *  3. Multi-level: child overrides parent same-type directive
 * ================================================================== */

TEST_F(DirWalkerTest, MultiLevel_ChildOverridesParent)
{
    /* Root: Header set X-Custom parent-val */
    htaccess_directive_t *root_dirs = make_directive(
        DIR_HEADER_SET, "X-Custom", "parent-val", 1);
    htaccess_cache_put("/var/www/html/.htaccess", 0, 0, 0, root_dirs);

    /* Child: Header set X-Custom child-val */
    htaccess_directive_t *child_dirs = make_directive(
        DIR_HEADER_SET, "X-Custom", "child-val", 1);
    htaccess_cache_put("/var/www/html/sub/.htaccess", 0, 0, 0, child_dirs);

    htaccess_directive_t *merged = htaccess_dirwalk(
        nullptr, "/var/www/html", "/var/www/html/sub");

    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(count_directives(merged), 1);

    auto *found = find_by_type_and_name(merged, DIR_HEADER_SET, "X-Custom");
    ASSERT_NE(found, nullptr);
    EXPECT_STREQ(found->value, "child-val");

    htaccess_directives_free(merged);
}

/* ==================================================================
 *  4. Empty directory (no .htaccess at any level)
 * ================================================================== */

TEST_F(DirWalkerTest, EmptyDirectory_NoHtaccess)
{
    /* No cache entries — all directories are empty */
    htaccess_directive_t *merged = htaccess_dirwalk(
        nullptr, "/var/www/html", "/var/www/html/empty/dir");

    EXPECT_EQ(merged, nullptr);
}

/* ==================================================================
 *  5. Middle directory without .htaccess doesn't affect inheritance
 * ================================================================== */

TEST_F(DirWalkerTest, MiddleDirWithoutHtaccess_InheritsFromRoot)
{
    /* Root has directives */
    htaccess_directive_t *root_dirs = make_directive(
        DIR_HEADER_SET, "X-Root", "root-val", 1);
    htaccess_cache_put("/var/www/html/.htaccess", 0, 0, 0, root_dirs);

    /* Middle dir (/var/www/html/mid) has NO .htaccess */
    /* Leaf dir (/var/www/html/mid/leaf) has NO .htaccess */

    htaccess_directive_t *merged = htaccess_dirwalk(
        nullptr, "/var/www/html", "/var/www/html/mid/leaf");

    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(count_directives(merged), 1);
    EXPECT_STREQ(merged->name, "X-Root");
    EXPECT_STREQ(merged->value, "root-val");

    htaccess_directives_free(merged);
}

/* ==================================================================
 *  6. Three-level deep override chain
 * ================================================================== */

TEST_F(DirWalkerTest, ThreeLevelDeep_OverrideChain)
{
    /* Root: Header set X-Level root */
    htaccess_directive_t *root_dirs = make_directive(
        DIR_HEADER_SET, "X-Level", "root", 1);
    htaccess_cache_put("/var/www/html/.htaccess", 0, 0, 0, root_dirs);

    /* Level 1: Header set X-Level level1 */
    htaccess_directive_t *l1_dirs = make_directive(
        DIR_HEADER_SET, "X-Level", "level1", 1);
    htaccess_cache_put("/var/www/html/a/.htaccess", 0, 0, 0, l1_dirs);

    /* Level 2: Header set X-Level level2 */
    htaccess_directive_t *l2_dirs = make_directive(
        DIR_HEADER_SET, "X-Level", "level2", 1);
    htaccess_cache_put("/var/www/html/a/b/.htaccess", 0, 0, 0, l2_dirs);

    htaccess_directive_t *merged = htaccess_dirwalk(
        nullptr, "/var/www/html", "/var/www/html/a/b");

    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(count_directives(merged), 1);

    auto *found = find_by_type_and_name(merged, DIR_HEADER_SET, "X-Level");
    ASSERT_NE(found, nullptr);
    EXPECT_STREQ(found->value, "level2");

    htaccess_directives_free(merged);
}

/* ==================================================================
 *  7. Multiple directive types across levels
 * ================================================================== */

TEST_F(DirWalkerTest, MultipleDirectiveTypes_AcrossLevels)
{
    /* Root: Header set X-A aaa + SetEnv VAR1 val1 */
    htaccess_directive_t *root_dirs = make_directive(
        DIR_HEADER_SET, "X-A", "aaa", 1);
    htaccess_directive_t *root_env = make_directive(
        DIR_SETENV, "VAR1", "val1", 2);
    append_directive(&root_dirs, root_env);
    htaccess_cache_put("/var/www/html/.htaccess", 0, 0, 0, root_dirs);

    /* Child: Header set X-A bbb (overrides) + SetEnv VAR2 val2 (new) */
    htaccess_directive_t *child_dirs = make_directive(
        DIR_HEADER_SET, "X-A", "bbb", 1);
    htaccess_directive_t *child_env = make_directive(
        DIR_SETENV, "VAR2", "val2", 2);
    append_directive(&child_dirs, child_env);
    htaccess_cache_put("/var/www/html/sub/.htaccess", 0, 0, 0, child_dirs);

    htaccess_directive_t *merged = htaccess_dirwalk(
        nullptr, "/var/www/html", "/var/www/html/sub");

    ASSERT_NE(merged, nullptr);
    /* X-A overridden, VAR1 inherited, VAR2 added = 3 directives */
    EXPECT_EQ(count_directives(merged), 3);

    auto *xa = find_by_type_and_name(merged, DIR_HEADER_SET, "X-A");
    ASSERT_NE(xa, nullptr);
    EXPECT_STREQ(xa->value, "bbb");

    auto *var1 = find_by_type_and_name(merged, DIR_SETENV, "VAR1");
    ASSERT_NE(var1, nullptr);
    EXPECT_STREQ(var1->value, "val1");

    auto *var2 = find_by_type_and_name(merged, DIR_SETENV, "VAR2");
    ASSERT_NE(var2, nullptr);
    EXPECT_STREQ(var2->value, "val2");

    htaccess_directives_free(merged);
}

/* ==================================================================
 *  8. Null parameters
 * ================================================================== */

TEST_F(DirWalkerTest, NullDocRoot_ReturnsNull)
{
    EXPECT_EQ(htaccess_dirwalk(nullptr, nullptr, "/var/www/html"), nullptr);
}

TEST_F(DirWalkerTest, NullTargetDir_ReturnsNull)
{
    EXPECT_EQ(htaccess_dirwalk(nullptr, "/var/www/html", nullptr), nullptr);
}

/* ==================================================================
 *  9. Target dir doesn't start with doc_root
 * ================================================================== */

TEST_F(DirWalkerTest, TargetNotUnderDocRoot_ReturnsNull)
{
    EXPECT_EQ(htaccess_dirwalk(nullptr, "/var/www/html", "/other/path"),
              nullptr);
}

/* ==================================================================
 *  10. Singleton directive override (ExpiresActive)
 * ================================================================== */

TEST_F(DirWalkerTest, SingletonDirective_ChildOverrides)
{
    /* Root: ExpiresActive On */
    htaccess_directive_t *root_dirs = make_directive(
        DIR_EXPIRES_ACTIVE, nullptr, nullptr, 1);
    root_dirs->data.expires.active = 1;
    htaccess_cache_put("/var/www/html/.htaccess", 0, 0, 0, root_dirs);

    /* Child: ExpiresActive Off */
    htaccess_directive_t *child_dirs = make_directive(
        DIR_EXPIRES_ACTIVE, nullptr, nullptr, 1);
    child_dirs->data.expires.active = 0;
    htaccess_cache_put("/var/www/html/sub/.htaccess", 0, 0, 0, child_dirs);

    htaccess_directive_t *merged = htaccess_dirwalk(
        nullptr, "/var/www/html", "/var/www/html/sub");

    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(count_directives(merged), 1);

    auto *found = find_by_type_and_name(merged, DIR_EXPIRES_ACTIVE, nullptr);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->data.expires.active, 0);

    htaccess_directives_free(merged);
}
