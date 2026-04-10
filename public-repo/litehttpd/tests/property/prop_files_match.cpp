/**
 * prop_files_match.cpp - Property-based tests for FilesMatch executor
 *
 * Feature: ols-htaccess-module
 *
 * Property 19: FilesMatch 条件应用
 *
 * Verifies that nested directives are applied if and only if the filename
 * matches the regex pattern.
 *
 * **Validates: Requirements 9.1, 9.2**
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <cstring>
#include <cstdlib>

#include "mock_lsiapi.h"
#include "gen_header.h"
#include "gen_regex.h"

extern "C" {
#include "htaccess_exec_files_match.h"
#include "htaccess_directive.h"
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/**
 * Create a Header set child directive (dynamically allocated).
 */
static htaccess_directive_t *make_header_set_child(const std::string &name,
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

/**
 * Create a FilesMatch directive with a single Header set child.
 */
static htaccess_directive_t *make_files_match_dir(const std::string &pattern,
                                                  htaccess_directive_t *child)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_FILES_MATCH;
    d->line_number = 1;
    d->name = nullptr;
    d->value = nullptr;
    d->data.files_match.pattern = strdup(pattern.c_str());
    d->data.files_match.children = child;
    d->next = nullptr;
    return d;
}

/**
 * Free a FilesMatch directive and its children.
 */
static void free_files_match_dir(htaccess_directive_t *d)
{
    if (!d) return;
    /* Free children */
    htaccess_directive_t *child = d->data.files_match.children;
    while (child) {
        htaccess_directive_t *next = child->next;
        free(child->name);
        free(child->value);
        free(child);
        child = next;
    }
    free(d->data.files_match.pattern);
    free(d->name);
    free(d->value);
    free(d);
}

/* ------------------------------------------------------------------ */
/*  Generators                                                         */
/* ------------------------------------------------------------------ */

namespace gen {

/**
 * Generate a filename with a specific extension.
 * Returns e.g. "index.php", "style.css", "app.js"
 */
inline rc::Gen<std::string> filenameWithExt(const std::string &ext)
{
    return rc::gen::map(
        alphaIdent(),
        [ext](const std::string &base) {
            return base + "." + ext;
        });
}

/**
 * Generate a filename that does NOT have the given extension.
 * Uses a different extension from a fixed set.
 */
inline rc::Gen<std::string> filenameWithoutExt(const std::string &ext)
{
    /* Pick an extension that differs from the target */
    auto otherExts = std::vector<std::string>{
        "bak", "tmp", "log", "dat", "bin", "old"};
    return rc::gen::map(
        rc::gen::pair(alphaIdent(), rc::gen::elementOf(otherExts)),
        [](const std::pair<std::string, std::string> &p) {
            return p.first + "." + p.second;
        });
}

} /* namespace gen */

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class FilesMatchPropertyFixture : public ::testing::Test {
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
/*  Property 19: FilesMatch 条件应用                                   */
/*                                                                     */
/*  For any regex pattern and filename, nested directives within a     */
/*  FilesMatch block are applied if and only if the filename matches   */
/*  the regex pattern.                                                 */
/*                                                                     */
/*  **Validates: Requirements 9.1, 9.2**                               */
/* ------------------------------------------------------------------ */

/**
 * When filename matches the pattern, the nested Header set directive
 * should be applied (header appears in response).
 */
RC_GTEST_FIXTURE_PROP(FilesMatchPropertyFixture,
                      MatchingFilenameAppliesNestedDirectives,
                      ())
{
    /* Pick a random extension and generate a matching filename */
    auto extensions = std::vector<std::string>{
        "php", "html", "css", "js", "png", "jpg", "gif", "txt"};
    auto ext = *rc::gen::elementOf(extensions);
    auto filename = *gen::filenameWithExt(ext);
    auto hdrName = *gen::headerName();
    auto hdrValue = *gen::headerValue();

    /* Pattern: .*\.ext$ — matches any file with the extension */
    std::string pattern = ".*\\." + ext + "$";

    auto *child = make_header_set_child(hdrName, hdrValue);
    auto *dir = make_files_match_dir(pattern, child);

    int rc = exec_files_match(session_.handle(), dir, filename.c_str());
    RC_ASSERT(rc == 1); /* 1 = matched and executed */

    /* Header should be set because filename matches */
    RC_ASSERT(session_.has_response_header(hdrName));
    RC_ASSERT(session_.get_response_header(hdrName) == hdrValue);

    free_files_match_dir(dir);
}

/**
 * When filename does NOT match the pattern, the nested Header set
 * directive should NOT be applied (header absent from response).
 */
RC_GTEST_FIXTURE_PROP(FilesMatchPropertyFixture,
                      NonMatchingFilenameSkipsNestedDirectives,
                      ())
{
    /* Pick a random extension for the pattern */
    auto extensions = std::vector<std::string>{
        "php", "html", "css", "js", "png", "jpg", "gif", "txt"};
    auto ext = *rc::gen::elementOf(extensions);
    /* Generate a filename that does NOT have this extension */
    auto filename = *gen::filenameWithoutExt(ext);
    auto hdrName = *gen::headerName();
    auto hdrValue = *gen::headerValue();

    /* Pattern: .*\.ext$ — should NOT match the filename */
    std::string pattern = ".*\\." + ext + "$";

    auto *child = make_header_set_child(hdrName, hdrValue);
    auto *dir = make_files_match_dir(pattern, child);

    int rc = exec_files_match(session_.handle(), dir, filename.c_str());
    RC_ASSERT(rc == 0); /* 0 = no match */

    /* Header should NOT be set because filename doesn't match */
    RC_ASSERT(!session_.has_response_header(hdrName));

    free_files_match_dir(dir);
}
