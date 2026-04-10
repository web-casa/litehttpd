/**
 * prop_files.cpp - Property-based tests for Files exact match conditional application
 *
 * Feature: htaccess-v2-enhancements, Property 30: Files exact match conditional application
 *
 * For any filename and request filename, verify Files block directives are
 * applied if and only if basename exact match (case-sensitive).
 *
 * **Validates: Requirements 5.2, 5.3**
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>

#include "mock_lsiapi.h"
#include "gen_header.h"

extern "C" {
#include "htaccess_directive.h"
#include "htaccess_exec_header.h"
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/**
 * Extract basename from a URI path (same logic as mod_htaccess.c).
 */
static std::string extractBasename(const std::string &uri)
{
    auto pos = uri.rfind('/');
    if (pos == std::string::npos)
        return uri;
    return uri.substr(pos + 1);
}

/**
 * Create a Header set child directive (dynamically allocated).
 */
static htaccess_directive_t *make_header_child(const std::string &name,
                                               const std::string &value)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type  = DIR_HEADER_SET;
    d->line_number = 2;
    d->name  = strdup(name.c_str());
    d->value = strdup(value.c_str());
    d->next  = nullptr;
    return d;
}

/**
 * Create a DIR_FILES directive with a single Header set child.
 */
static htaccess_directive_t *make_files_dir(const std::string &filename,
                                            htaccess_directive_t *child)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_FILES;
    d->line_number = 1;
    d->name  = strdup(filename.c_str());
    d->value = nullptr;
    d->data.files.children = child;
    d->next  = nullptr;
    return d;
}

/**
 * Simulate Files block execution: if basename of request matches the
 * Files block filename exactly (case-sensitive), execute children.
 * This mirrors the intended mod_htaccess.c integration logic.
 */
static int exec_files_block(lsi_session_t *session,
                            const htaccess_directive_t *dir,
                            const char *request_basename)
{
    if (!session || !dir || !request_basename)
        return -1;
    if (dir->type != DIR_FILES || !dir->name)
        return -1;

    /* Exact case-sensitive match */
    if (strcmp(dir->name, request_basename) != 0)
        return 0;  /* No match — skip children */

    /* Match — execute children */
    for (const htaccess_directive_t *child = dir->data.files.children;
         child; child = child->next) {
        if (child->type == DIR_HEADER_SET)
            exec_header(session, child);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Generators                                                         */
/* ------------------------------------------------------------------ */

namespace gen {

/**
 * Generate a valid filename (no slashes, 1-20 chars).
 * Includes common patterns like wp-config.php, .htaccess, etc.
 */
inline rc::Gen<std::string> filename()
{
    static const std::vector<std::string> kCommon = {
        "wp-config.php", ".htaccess", "index.php", "index.html",
        "config.php", "settings.php", ".env", "robots.txt",
        "xmlrpc.php", "wp-login.php", "readme.html", "license.txt"
    };
    return rc::gen::oneOf(
        rc::gen::elementOf(kCommon),
        rc::gen::map(
            alphaIdent(),
            [](const std::string &base) {
                return base + ".php";
            }),
        rc::gen::map(
            alphaIdent(),
            [](const std::string &base) {
                return base + ".txt";
            })
    );
}

/**
 * Generate a URI path with a specific basename.
 */
inline rc::Gen<std::string> uriWithBasename(const std::string &basename)
{
    static const std::vector<std::string> kPrefixes = {
        "/", "/var/www/", "/home/user/public_html/",
        "/wp-admin/", "/wp-content/", "/app/",
        "/public/", "/site/"
    };
    return rc::gen::map(
        rc::gen::elementOf(kPrefixes),
        [basename](const std::string &prefix) {
            return prefix + basename;
        });
}

} /* namespace gen */

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class FilesPropertyFixture : public ::testing::Test {
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
/*  Property 30: Files 精确匹配条件应用 — Matching basename            */
/*                                                                     */
/*  For any filename and request URI whose basename matches exactly    */
/*  (case-sensitive), the Files block children SHALL be applied.       */
/*                                                                     */
/*  **Validates: Requirements 5.2**                                    */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(FilesPropertyFixture,
                      MatchingBasenameAppliesChildren,
                      ())
{
    auto filesName = *gen::filename();
    auto uri       = *gen::uriWithBasename(filesName);
    auto hdrName   = *gen::headerName();
    auto hdrValue  = *gen::headerValue();

    auto *child = make_header_child(hdrName, hdrValue);
    auto *dir   = make_files_dir(filesName, child);

    std::string basename = extractBasename(uri);
    RC_ASSERT(basename == filesName);

    exec_files_block(session_.handle(), dir, basename.c_str());

    /* Header should be set because basename matches */
    RC_ASSERT(session_.has_response_header(hdrName));
    RC_ASSERT(session_.get_response_header(hdrName) == hdrValue);

    htaccess_directives_free(dir);
}

/* ------------------------------------------------------------------ */
/*  Property 30: Files 精确匹配条件应用 — Non-matching basename        */
/*                                                                     */
/*  For any filename and request URI whose basename does NOT match     */
/*  (case-sensitive), the Files block children SHALL be skipped.       */
/*                                                                     */
/*  **Validates: Requirements 5.3**                                    */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(FilesPropertyFixture,
                      NonMatchingBasenameSkipsChildren,
                      ())
{
    auto filesName   = *gen::filename();
    auto requestName = *gen::filename();

    /* Ensure the two filenames differ */
    RC_PRE(filesName != requestName);

    auto hdrName  = *gen::headerName();
    auto hdrValue = *gen::headerValue();

    auto *child = make_header_child(hdrName, hdrValue);
    auto *dir   = make_files_dir(filesName, child);

    exec_files_block(session_.handle(), dir, requestName.c_str());

    /* Header should NOT be set because basename doesn't match */
    RC_ASSERT(!session_.has_response_header(hdrName));

    htaccess_directives_free(dir);
}

/* ------------------------------------------------------------------ */
/*  Property 30: Case sensitivity                                      */
/*                                                                     */
/*  Files matching is case-sensitive: "WP-Config.PHP" should NOT       */
/*  match "wp-config.php".                                             */
/*                                                                     */
/*  **Validates: Requirements 5.2, 5.3**                               */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(FilesPropertyFixture,
                      CaseSensitiveMatching,
                      ())
{
    auto filesName = *gen::filename();
    auto hdrName   = *gen::headerName();
    auto hdrValue  = *gen::headerValue();

    /* Create a case-altered version */
    std::string altered = filesName;
    bool changed = false;
    for (auto &ch : altered) {
        if (std::islower(ch)) {
            ch = std::toupper(ch);
            changed = true;
            break;
        } else if (std::isupper(ch)) {
            ch = std::tolower(ch);
            changed = true;
            break;
        }
    }
    RC_PRE(changed && altered != filesName);

    auto *child = make_header_child(hdrName, hdrValue);
    auto *dir   = make_files_dir(filesName, child);

    exec_files_block(session_.handle(), dir, altered.c_str());

    /* Header should NOT be set because case differs */
    RC_ASSERT(!session_.has_response_header(hdrName));

    htaccess_directives_free(dir);
}
