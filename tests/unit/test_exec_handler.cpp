/**
 * test_exec_handler.cpp - Unit tests for AddHandler/SetHandler/AddType
 *
 * Tests parsing, printing, and execution of handler/type directives.
 */
#include <gtest/gtest.h>
#include <cstring>
#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_parser.h"
#include "htaccess_printer.h"
#include "htaccess_exec_handler.h"
#include "htaccess_directive.h"
}

class HandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
    MockSession session_;
};

/* --- Parsing tests --- */

TEST_F(HandlerTest, ParseAddHandler) {
    const char *input = "AddHandler cgi-script .cgi .pl\n";
    htaccess_directive_t *d = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_ADD_HANDLER);
    EXPECT_STREQ(d->name, "cgi-script");
    EXPECT_STREQ(d->value, ".cgi .pl");
    htaccess_directives_free(d);
}

TEST_F(HandlerTest, ParseSetHandler) {
    const char *input = "SetHandler proxy:fcgi://localhost:9000\n";
    htaccess_directive_t *d = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_SET_HANDLER);
    EXPECT_STREQ(d->value, "proxy:fcgi://localhost:9000");
    htaccess_directives_free(d);
}

TEST_F(HandlerTest, ParseAddType) {
    const char *input = "AddType application/x-httpd-php .php .phtml\n";
    htaccess_directive_t *d = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_ADD_TYPE);
    EXPECT_STREQ(d->name, "application/x-httpd-php");
    EXPECT_STREQ(d->value, ".php .phtml");
    htaccess_directives_free(d);
}

TEST_F(HandlerTest, ParseDirectoryIndex) {
    const char *input = "DirectoryIndex index.html index.php default.htm\n";
    htaccess_directive_t *d = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_DIRECTORY_INDEX);
    EXPECT_STREQ(d->value, "index.html index.php default.htm");
    htaccess_directives_free(d);
}

/* --- Execution tests --- */

TEST_F(HandlerTest, AddTypeMatchesExtension) {
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ADD_TYPE;
    d->name = strdup("text/css");
    d->value = strdup(".css");

    int rc = exec_add_type(session_.handle(), d, "style.css");
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_STREQ(session_.get_response_header("Content-Type").c_str(), "text/css");
    htaccess_directives_free(d);
}

TEST_F(HandlerTest, AddTypeNoMatchSkips) {
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ADD_TYPE;
    d->name = strdup("text/css");
    d->value = strdup(".css");

    int rc = exec_add_type(session_.handle(), d, "script.js");
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_TRUE(session_.get_response_header("Content-Type").empty());
    htaccess_directives_free(d);
}

TEST_F(HandlerTest, AddTypeMultipleExtensions) {
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ADD_TYPE;
    d->name = strdup("application/x-httpd-php");
    d->value = strdup(".php .phtml .php5");

    int rc = exec_add_type(session_.handle(), d, "index.phtml");
    EXPECT_EQ(rc, LSI_OK);
    EXPECT_STREQ(session_.get_response_header("Content-Type").c_str(),
                 "application/x-httpd-php");
    htaccess_directives_free(d);
}

TEST_F(HandlerTest, AddHandlerReturnsOk) {
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ADD_HANDLER;
    d->name = strdup("cgi-script");
    d->value = strdup(".cgi");
    EXPECT_EQ(exec_add_handler(session_.handle(), d), LSI_OK);
    htaccess_directives_free(d);
}

TEST_F(HandlerTest, SetHandlerReturnsOk) {
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_SET_HANDLER;
    d->value = strdup("proxy:fcgi://localhost:9000");
    EXPECT_EQ(exec_set_handler(session_.handle(), d), LSI_OK);
    htaccess_directives_free(d);
}
