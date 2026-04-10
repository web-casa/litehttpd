/**
 * prop_auth_basic.cpp - Property-based tests for AuthType Basic
 *
 * Feature: htaccess-v2-enhancements, Properties 37-38
 *
 * Property 37: No/wrong credentials → 401, correct credentials → pass
 * Property 38: htpasswd_check returns match iff hash is valid for password
 *
 * Validates: Requirements 10.4-10.7
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_auth.h"
#include "htaccess_directive.h"
}

/* Base64 encode */
static std::string b64encode(const std::string &input) {
    static const char t[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) { out.push_back(t[(val >> valb) & 0x3F]); valb -= 6; }
    }
    if (valb > -6) out.push_back(t[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

/* Generate a simple alphanumeric password (1-8 chars) */
static rc::Gen<std::string> genPassword()
{
    return rc::gen::suchThat(
        rc::gen::container<std::string>(rc::gen::inRange((char)'a', (char)('z' + 1))),
        [](const std::string &s) { return s.size() >= 1 && s.size() <= 8; });
}

/* Generate a simple username (3-8 chars) */
static rc::Gen<std::string> genUsername()
{
    return rc::gen::suchThat(
        rc::gen::container<std::string>(rc::gen::inRange((char)'a', (char)('z' + 1))),
        [](const std::string &s) { return s.size() >= 3 && s.size() <= 8; });
}

/* Property 38: htpasswd_check matches iff hash is valid for password */
RC_GTEST_PROP(AuthBasicProp, HtpasswdCheckMatchesCorrectPassword, ())
{
    auto password = *genPassword();

    /* Generate crypt hash */
    const char *raw = crypt(password.c_str(), "ab");
    RC_PRE(raw != nullptr);
    std::string hash(raw);

    /* Correct password should match */
    RC_ASSERT(htpasswd_check(hash.c_str(), password.c_str()) == 1);
}

RC_GTEST_PROP(AuthBasicProp, HtpasswdCheckRejectsWrongPassword, ())
{
    auto password = *genPassword();
    auto wrong = *genPassword();
    RC_PRE(password != wrong);

    const char *raw = crypt(password.c_str(), "ab");
    RC_PRE(raw != nullptr);
    std::string hash(raw);

    RC_ASSERT(htpasswd_check(hash.c_str(), wrong.c_str()) == 0);
}

/* Property 37: Auth flow — no credentials → 401 */
class AuthBasicPropFixture : public ::testing::Test {
public:
    void SetUp() override {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }
protected:
    MockSession session_;
};

RC_GTEST_FIXTURE_PROP(AuthBasicPropFixture, NoCredentialsReturns401, ())
{
    auto user = *genUsername();
    auto pass = *genPassword();

    /* Create temp htpasswd file */
    char tmppath[] = "/tmp/htpasswd_XXXXXX";
    int fd = mkstemp(tmppath);
    RC_PRE(fd >= 0);

    const char *raw = crypt(pass.c_str(), "ab");
    RC_PRE(raw != nullptr);
    std::string hash(raw);

    std::string line = user + ":" + hash + "\n";
    ssize_t wr = write(fd, line.c_str(), line.size());
    RC_PRE(wr > 0);
    close(fd);

    /* Build directive list */
    auto *d1 = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d1->type = DIR_AUTH_TYPE; d1->value = strdup("Basic");
    auto *d2 = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d2->type = DIR_AUTH_NAME; d2->value = strdup("Test");
    auto *d3 = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d3->type = DIR_AUTH_USER_FILE; d3->value = strdup(tmppath);
    auto *d4 = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d4->type = DIR_REQUIRE_VALID_USER;
    d1->next = d2; d2->next = d3; d3->next = d4;

    /* No auth header → should get 401 */
    int rc = exec_auth_basic(session_.handle(), d1);
    RC_ASSERT(rc == LSI_ERROR);
    RC_ASSERT(session_.get_status_code() == 401);

    unlink(tmppath);
    htaccess_directives_free(d1);
}

RC_GTEST_FIXTURE_PROP(AuthBasicPropFixture, CorrectCredentialsPass, ())
{
    auto user = *genUsername();
    auto pass = *genPassword();

    char tmppath[] = "/tmp/htpasswd_XXXXXX";
    int fd = mkstemp(tmppath);
    RC_PRE(fd >= 0);

    const char *raw = crypt(pass.c_str(), "ab");
    RC_PRE(raw != nullptr);
    std::string hash(raw);

    std::string line = user + ":" + hash + "\n";
    ssize_t wr = write(fd, line.c_str(), line.size());
    RC_PRE(wr > 0);
    close(fd);

    auto *d1 = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d1->type = DIR_AUTH_TYPE; d1->value = strdup("Basic");
    auto *d2 = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d2->type = DIR_AUTH_NAME; d2->value = strdup("Test");
    auto *d3 = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d3->type = DIR_AUTH_USER_FILE; d3->value = strdup(tmppath);
    auto *d4 = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d4->type = DIR_REQUIRE_VALID_USER;
    d1->next = d2; d2->next = d3; d3->next = d4;

    std::string auth = "Basic " + b64encode(user + ":" + pass);
    session_.set_auth_header(auth);

    int rc = exec_auth_basic(session_.handle(), d1);
    RC_ASSERT(rc == LSI_OK);

    unlink(tmppath);
    htaccess_directives_free(d1);
}
