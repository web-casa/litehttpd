/**
 * prop_dir_inherit.cpp - Property-based tests for directory hierarchy inheritance
 *
 * Feature: ols-htaccess-module
 *
 * Property 24: 目录层级继承
 *   For any directory hierarchy from doc root to target:
 *   (a) Child same-type directives override parent directives
 *   (b) Directories without .htaccess don't affect inheritance
 *   (c) Processing order is root to target
 *
 *   **Validates: Requirements 13.1, 13.2, 13.3**
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <unordered_map>

extern "C" {
#include "htaccess_cache.h"
#include "htaccess_directive.h"
#include "htaccess_dirwalker.h"
}

#include "gen_directive.h"
#include "gen_directory.h"

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_header_set(const char *name,
                                              const char *value,
                                              int line)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_HEADER_SET;
    d->line_number = line;
    d->name = strdup(name);
    d->value = strdup(value);
    d->next = nullptr;
    return d;
}

static htaccess_directive_t *make_setenv(const char *name,
                                          const char *value,
                                          int line)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_SETENV;
    d->line_number = line;
    d->name = strdup(name);
    d->value = strdup(value);
    d->next = nullptr;
    return d;
}

/** Count directives in a linked list. */
static int count_directives(const htaccess_directive_t *head)
{
    int n = 0;
    for (const htaccess_directive_t *d = head; d; d = d->next)
        n++;
    return n;
}

/** Find a directive by type and name in a list. */
static const htaccess_directive_t *find_directive(
    const htaccess_directive_t *head,
    directive_type_t type,
    const char *name)
{
    for (const htaccess_directive_t *d = head; d; d = d->next) {
        if (d->type == type && d->name && name && strcmp(d->name, name) == 0)
            return d;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class DirInheritPropertyFixture : public ::testing::Test {
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
/*  Property 24: 目录层级继承                                          */
/*                                                                     */
/*  (a) Child same-type directives override parent                     */
/*  (b) Directories without .htaccess don't affect inheritance         */
/*  (c) Processing order is root to target                             */
/*                                                                     */
/*  **Validates: Requirements 13.1, 13.2, 13.3**                      */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(DirInheritPropertyFixture,
                      ChildOverridesParentSameType,
                      ())
{
    /*
     * Strategy: Generate a random header name. Place a Header set directive
     * with value "parent" at the root level and "child" at a subdirectory.
     * After dirwalk, the merged result should have the child's value.
     */
    auto headerName = *gen::headerName();
    auto subDirName = *gen::dirName();

    std::string doc_root = "/var/www/html";
    std::string target = doc_root + "/" + subDirName;

    std::string root_htaccess = doc_root + "/.htaccess";
    std::string child_htaccess = target + "/.htaccess";

    /* Parent: Header set <name> parent_value */
    htaccess_directive_t *parent_dirs = make_header_set(
        headerName.c_str(), "parent_value", 1);

    /* Child: Header set <name> child_value */
    htaccess_directive_t *child_dirs = make_header_set(
        headerName.c_str(), "child_value", 1);

    /* Pre-populate cache with mtime=0 (stat on non-existent files returns 0) */
    htaccess_cache_put(root_htaccess.c_str(), 0, 0, 0, parent_dirs);
    htaccess_cache_put(child_htaccess.c_str(), 0, 0, 0, child_dirs);

    /* Run dirwalk */
    htaccess_directive_t *merged = htaccess_dirwalk(
        nullptr, doc_root.c_str(), target.c_str());

    RC_ASSERT(merged != nullptr);

    /* The merged result should have the child's value for this header */
    const htaccess_directive_t *found = find_directive(
        merged, DIR_HEADER_SET, headerName.c_str());
    RC_ASSERT(found != nullptr);
    RC_ASSERT(strcmp(found->value, "child_value") == 0);

    htaccess_directives_free(merged);
}

RC_GTEST_FIXTURE_PROP(DirInheritPropertyFixture,
                      EmptyDirDoesNotAffectInheritance,
                      ())
{
    /*
     * Strategy: Place directives at root level. Have a middle directory
     * with NO .htaccess (not in cache). The target directory also has
     * no .htaccess. The merged result should contain the root's directives.
     */
    auto headerName = *gen::headerName();
    auto midDirName = *gen::dirName();
    auto leafDirName = *gen::dirName();

    /* Ensure different names */
    RC_PRE(midDirName != leafDirName);

    std::string doc_root = "/var/www/html";
    std::string target = doc_root + "/" + midDirName + "/" + leafDirName;

    std::string root_htaccess = doc_root + "/.htaccess";

    /* Root: Header set <name> root_value */
    htaccess_directive_t *root_dirs = make_header_set(
        headerName.c_str(), "root_value", 1);

    /* Only populate cache for root with mtime=0 — middle and leaf have no .htaccess */
    htaccess_cache_put(root_htaccess.c_str(), 0, 0, 0, root_dirs);

    /* Run dirwalk */
    htaccess_directive_t *merged = htaccess_dirwalk(
        nullptr, doc_root.c_str(), target.c_str());

    RC_ASSERT(merged != nullptr);

    /* Root's directive should be preserved */
    const htaccess_directive_t *found = find_directive(
        merged, DIR_HEADER_SET, headerName.c_str());
    RC_ASSERT(found != nullptr);
    RC_ASSERT(strcmp(found->value, "root_value") == 0);

    htaccess_directives_free(merged);
}

RC_GTEST_FIXTURE_PROP(DirInheritPropertyFixture,
                      ProcessingOrderRootToTarget,
                      ())
{
    /*
     * Strategy: Place non-overlapping directives at different levels.
     * Root has Header set X-Root, child has SetEnv MY_VAR.
     * Both should appear in the merged result (non-overlapping types
     * from different levels are all preserved).
     */
    auto subDirName = *gen::dirName();

    std::string doc_root = "/var/www/html";
    std::string target = doc_root + "/" + subDirName;

    std::string root_htaccess = doc_root + "/.htaccess";
    std::string child_htaccess = target + "/.htaccess";

    /* Root: Header set X-Root root_val */
    htaccess_directive_t *root_dirs = make_header_set(
        "X-Root", "root_val", 1);

    /* Child: SetEnv MY_VAR child_val */
    htaccess_directive_t *child_dirs = make_setenv(
        "MY_VAR", "child_val", 1);

    htaccess_cache_put(root_htaccess.c_str(), 0, 0, 0, root_dirs);
    htaccess_cache_put(child_htaccess.c_str(), 0, 0, 0, child_dirs);

    /* Run dirwalk */
    htaccess_directive_t *merged = htaccess_dirwalk(
        nullptr, doc_root.c_str(), target.c_str());

    RC_ASSERT(merged != nullptr);

    /* Both directives should be present */
    const htaccess_directive_t *header = find_directive(
        merged, DIR_HEADER_SET, "X-Root");
    RC_ASSERT(header != nullptr);
    RC_ASSERT(strcmp(header->value, "root_val") == 0);

    const htaccess_directive_t *env = find_directive(
        merged, DIR_SETENV, "MY_VAR");
    RC_ASSERT(env != nullptr);
    RC_ASSERT(strcmp(env->value, "child_val") == 0);

    htaccess_directives_free(merged);
}
