/**
 * prop_encoding_charset.cpp - Property 42: AddEncoding/AddCharset header setting
 *
 * Feature: htaccess-v2-enhancements, Property 42
 * Validates: Requirements 14.2, 15.2
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <cstring>
#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_encoding.h"
#include "htaccess_directive.h"
}

static rc::Gen<std::string> genEncoding()
{
    auto encs = std::vector<std::string>{"gzip", "deflate", "br", "compress"};
    return rc::gen::elementOf(encs);
}

static rc::Gen<std::string> genCharset()
{
    auto cs = std::vector<std::string>{
        "UTF-8", "ISO-8859-1", "US-ASCII", "UTF-16", "EUC-JP"};
    return rc::gen::elementOf(cs);
}

static rc::Gen<std::string> genExt()
{
    auto exts = std::vector<std::string>{
        ".gz", ".html", ".css", ".js", ".txt", ".xml", ".json"};
    return rc::gen::elementOf(exts);
}

class EncodingPropFixture : public ::testing::Test {
public:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
protected:
    MockSession session_;
};

RC_GTEST_FIXTURE_PROP(EncodingPropFixture, AddEncodingSetsHeader, ())
{
    auto enc = *genEncoding();
    auto ext = *genExt();
    std::string filename = "file" + ext;

    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ADD_ENCODING;
    d->name = strdup(enc.c_str());
    d->value = strdup(ext.c_str());

    int rc = exec_add_encoding(session_.handle(), d, filename.c_str());
    RC_ASSERT(rc == LSI_OK);
    RC_ASSERT(session_.get_response_header("Content-Encoding") == enc);

    htaccess_directives_free(d);
}

RC_GTEST_FIXTURE_PROP(EncodingPropFixture, AddCharsetAppendsCharset, ())
{
    auto cs = *genCharset();
    auto ext = *genExt();
    std::string filename = "page" + ext;

    session_.add_response_header("Content-Type", "text/html");

    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_ADD_CHARSET;
    d->name = strdup(cs.c_str());
    d->value = strdup(ext.c_str());

    int rc = exec_add_charset(session_.handle(), d, filename.c_str());
    RC_ASSERT(rc == LSI_OK);

    std::string expected = "text/html; charset=" + cs;
    RC_ASSERT(session_.get_response_header("Content-Type") == expected);

    htaccess_directives_free(d);
}
