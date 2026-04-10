/**
 * prop_forcetype.cpp - Property 41: ForceType Content-Type override
 *
 * Feature: htaccess-v2-enhancements, Property 41
 * Validates: Requirements 13.2
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <cstring>
#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_forcetype.h"
#include "htaccess_directive.h"
}

static rc::Gen<std::string> genMime()
{
    auto types = std::vector<std::string>{
        "text/html", "text/css", "text/plain", "application/json",
        "application/xml", "application/pdf", "image/png", "image/jpeg"};
    return rc::gen::elementOf(types);
}

class ForceTypePropFixture : public ::testing::Test {
public:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
protected:
    MockSession session_;
};

RC_GTEST_FIXTURE_PROP(ForceTypePropFixture, SetsContentType, ())
{
    auto mime = *genMime();

    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_FORCE_TYPE;
    d->value = strdup(mime.c_str());

    int rc = exec_force_type(session_.handle(), d);
    RC_ASSERT(rc == LSI_OK);
    RC_ASSERT(session_.get_response_header("Content-Type") == mime);

    htaccess_directives_free(d);
}

RC_GTEST_FIXTURE_PROP(ForceTypePropFixture, OverridesPrevious, ())
{
    auto prev = *genMime();
    auto next = *genMime();
    RC_PRE(prev != next);

    session_.add_response_header("Content-Type", prev);

    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_FORCE_TYPE;
    d->value = strdup(next.c_str());

    int rc = exec_force_type(session_.handle(), d);
    RC_ASSERT(rc == LSI_OK);
    RC_ASSERT(session_.get_response_header("Content-Type") == next);

    htaccess_directives_free(d);
}
