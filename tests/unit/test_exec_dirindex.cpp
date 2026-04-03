/**
 * test_exec_dirindex.cpp - Unit tests for DirectoryIndex executor
 */
#include <gtest/gtest.h>
#include <cstring>
#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_dirindex.h"
#include "htaccess_directive.h"
}

class DirIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
    MockSession session_;
};

TEST_F(DirIndexTest, SingleFileExists) {
    session_.set_request_uri("/app/");
    session_.add_existing_file("/var/www/html/app/index.html");
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_DIRECTORY_INDEX;
    d->value = strdup("index.html");

    int rc = exec_directory_index(session_.handle(), d, "/var/www/html/app");
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_EQ(session_.get_internal_uri(), "/app/index.html");
    htaccess_directives_free(d);
}

TEST_F(DirIndexTest, FirstExistingFileSelected) {
    /* index.html doesn't exist, index.php does */
    session_.set_request_uri("/");
    session_.add_existing_file("/var/www/html/index.php");
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_DIRECTORY_INDEX;
    d->value = strdup("index.html index.php default.htm");

    int rc = exec_directory_index(session_.handle(), d, "/var/www/html");
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_EQ(session_.get_internal_uri(), "/index.php");
    htaccess_directives_free(d);
}

TEST_F(DirIndexTest, NoFileExistsFallback) {
    session_.set_request_uri("/");
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_DIRECTORY_INDEX;
    d->value = strdup("index.html index.php");

    int rc = exec_directory_index(session_.handle(), d, "/var/www/html");
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_TRUE(session_.get_internal_uri().empty());
    htaccess_directives_free(d);
}

TEST_F(DirIndexTest, TrailingSlashInDir) {
    session_.set_request_uri("/subdir/");
    session_.add_existing_file("/var/www/html/subdir/index.html");
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_DIRECTORY_INDEX;
    d->value = strdup("index.html");

    int rc = exec_directory_index(session_.handle(), d, "/var/www/html/subdir/");
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_EQ(session_.get_internal_uri(), "/subdir/index.html");
    htaccess_directives_free(d);
}
