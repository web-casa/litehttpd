/**
 * prop_dirindex.cpp - Property-based tests for DirectoryIndex
 *
 * Feature: htaccess-v2-enhancements, Property 40
 *
 * Property 40: For any DirectoryIndex file list and directory contents,
 * the executor selects the first existing file from the list.
 *
 * Validates: Requirements 12.2
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <vector>
#include <cstring>
#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_dirindex.h"
#include "htaccess_directive.h"
}

static rc::Gen<std::string> genFilename()
{
    auto names = std::vector<std::string>{
        "index.html", "index.php", "default.htm", "home.html",
        "main.php", "app.html", "start.htm", "welcome.html"};
    return rc::gen::elementOf(names);
}

class DirIndexPropFixture : public ::testing::Test {
public:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
protected:
    MockSession session_;
};

RC_GTEST_FIXTURE_PROP(DirIndexPropFixture, SelectsFirstExistingFile, ())
{
    /* Use a fixed set of filenames and pick a subset */
    std::vector<std::string> allFiles = {
        "index.html", "index.php", "default.htm", "home.html"};
    auto mask = *rc::gen::container<std::vector<bool>>(4, rc::gen::arbitrary<bool>());

    std::vector<std::string> files;
    for (size_t i = 0; i < 4; i++) {
        if (mask[i]) files.push_back(allFiles[i]);
    }
    RC_PRE(files.size() >= 2);

    /* Pick one random file to "exist" */
    auto existIdx = *rc::gen::inRange((size_t)0, files.size());

    std::string dir = "/var/www";
    std::string uri = "/app/";
    session_.set_request_uri(uri);
    session_.add_existing_file(dir + "/" + files[existIdx]);

    /* Build space-separated list */
    std::string list;
    for (size_t i = 0; i < files.size(); i++) {
        if (i > 0) list += " ";
        list += files[i];
    }

    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_DIRECTORY_INDEX;
    d->value = strdup(list.c_str());

    int rc = exec_directory_index(session_.handle(), d, dir.c_str());
    RC_ASSERT(rc == LSI_OK);

    /* The selected file should be the first existing one — redirect uses URI */
    std::string expected = uri + files[existIdx];
    RC_ASSERT(session_.get_internal_uri() == expected);

    htaccess_directives_free(d);
}

RC_GTEST_FIXTURE_PROP(DirIndexPropFixture, NoExistingFileFallsBack, ())
{
    std::vector<std::string> allFiles = {
        "index.html", "index.php", "default.htm"};
    auto count = *rc::gen::inRange(1, 4);

    std::string list;
    for (int i = 0; i < count; i++) {
        if (i > 0) list += " ";
        list += allFiles[i];
    }

    session_.set_request_uri("/");

    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_DIRECTORY_INDEX;
    d->value = strdup(list.c_str());

    int rc = exec_directory_index(session_.handle(), d, "/var/www");
    RC_ASSERT(rc == LSI_OK);
    RC_ASSERT(session_.get_internal_uri().empty());

    htaccess_directives_free(d);
}
