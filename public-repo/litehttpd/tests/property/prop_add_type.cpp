/**
 * prop_add_type.cpp - Property-based tests for AddType Content-Type setting
 *
 * Feature: htaccess-v2-enhancements, Property 39
 *
 * Property 39: For any AddType directive with MIME type and extension list,
 * when the request filename matches an extension, Content-Type is set.
 *
 * Validates: Requirements 11.5
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <cstring>
#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_handler.h"
#include "htaccess_directive.h"
}

/* Generate a simple MIME type like "text/plain" */
static rc::Gen<std::string> genMime()
{
    auto types = std::vector<std::string>{
        "text/html", "text/css", "text/plain",
        "application/json", "application/xml",
        "application/javascript", "image/png", "image/jpeg"};
    return rc::gen::elementOf(types);
}

/* Generate a file extension like ".html" */
static rc::Gen<std::string> genExt()
{
    auto exts = std::vector<std::string>{
        ".html", ".css", ".js", ".json", ".xml",
        ".png", ".jpg", ".php", ".txt", ".svg"};
    return rc::gen::elementOf(exts);
}

/* Generate a simple filename base */
static rc::Gen<std::string> genBasename()
{
    auto names = std::vector<std::string>{
        "index", "style", "app", "data", "config",
        "main", "test", "page", "script", "image"};
    return rc::gen::elementOf(names);
}

class AddTypePropFixture : public ::testing::Test {
public:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
protected:
    MockSession session_;
};

RC_GTEST_FIXTURE_PROP(AddTypePropFixture, MatchingExtSetsContentType, ())
{
    auto mime = *genMime();
    auto ext = *genExt();
    auto base = *genBasename();
    std::string filename = base + ext;

    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ADD_TYPE;
    d->name = strdup(mime.c_str());
    d->value = strdup(ext.c_str());

    int rc = exec_add_type(session_.handle(), d, filename.c_str());
    RC_ASSERT(rc == LSI_OK);
    RC_ASSERT(session_.get_response_header("Content-Type") == mime);

    htaccess_directives_free(d);
}

RC_GTEST_FIXTURE_PROP(AddTypePropFixture, NonMatchingExtNoContentType, ())
{
    auto mime = *genMime();
    auto ext = *genExt();
    auto otherExt = *genExt();
    RC_PRE(ext != otherExt);
    auto base = *genBasename();
    std::string filename = base + otherExt;

    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ADD_TYPE;
    d->name = strdup(mime.c_str());
    d->value = strdup(ext.c_str());

    int rc = exec_add_type(session_.handle(), d, filename.c_str());
    RC_ASSERT(rc == LSI_OK);
    RC_ASSERT(session_.get_response_header("Content-Type").empty());

    htaccess_directives_free(d);
}
