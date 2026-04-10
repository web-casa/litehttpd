/**
 * test_exec_encoding.cpp - Unit tests for AddEncoding/AddCharset executors
 */
#include <gtest/gtest.h>
#include <cstring>
#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_encoding.h"
#include "htaccess_parser.h"
#include "htaccess_directive.h"
}

class EncodingTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
    MockSession session_;
};

/* --- Parsing --- */

TEST_F(EncodingTest, ParseAddEncoding) {
    const char *input = "AddEncoding gzip .gz .tgz\n";
    auto *d = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_ADD_ENCODING);
    EXPECT_STREQ(d->name, "gzip");
    EXPECT_STREQ(d->value, ".gz .tgz");
    htaccess_directives_free(d);
}

TEST_F(EncodingTest, ParseAddCharset) {
    const char *input = "AddCharset UTF-8 .html .txt\n";
    auto *d = htaccess_parse(input, strlen(input), "test");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, DIR_ADD_CHARSET);
    EXPECT_STREQ(d->name, "UTF-8");
    EXPECT_STREQ(d->value, ".html .txt");
    htaccess_directives_free(d);
}

/* --- Execution --- */

TEST_F(EncodingTest, AddEncodingSetsHeader) {
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ADD_ENCODING;
    d->name = strdup("gzip");
    d->value = strdup(".gz");

    EXPECT_EQ(exec_add_encoding(session_.handle(), d, "archive.gz"), LSI_OK);
    EXPECT_EQ(session_.get_response_header("Content-Encoding"), "gzip");
    htaccess_directives_free(d);
}

TEST_F(EncodingTest, AddEncodingNoMatch) {
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ADD_ENCODING;
    d->name = strdup("gzip");
    d->value = strdup(".gz");

    EXPECT_EQ(exec_add_encoding(session_.handle(), d, "file.txt"), LSI_OK);
    EXPECT_TRUE(session_.get_response_header("Content-Encoding").empty());
    htaccess_directives_free(d);
}

TEST_F(EncodingTest, AddCharsetAppendsToContentType) {
    session_.add_response_header("Content-Type", "text/html");
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ADD_CHARSET;
    d->name = strdup("UTF-8");
    d->value = strdup(".html");

    EXPECT_EQ(exec_add_charset(session_.handle(), d, "page.html"), LSI_OK);
    EXPECT_EQ(session_.get_response_header("Content-Type"),
              "text/html; charset=UTF-8");
    htaccess_directives_free(d);
}

TEST_F(EncodingTest, AddCharsetNoMatch) {
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ADD_CHARSET;
    d->name = strdup("UTF-8");
    d->value = strdup(".html");

    EXPECT_EQ(exec_add_charset(session_.handle(), d, "data.json"), LSI_OK);
    EXPECT_TRUE(session_.get_response_header("Content-Type").empty());
    htaccess_directives_free(d);
}
