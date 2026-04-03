/**
 * prop_php_blacklist.cpp - Property-based tests for PHP blacklist correctness
 *
 * Feature: htaccess-v2-enhancements, Property 26: PHP blacklist correctness
 *
 * For any PHP setting name, verify exec_php_value() accepts it if and only
 * if the name is NOT in php_ini_system_settings[].
 *
 * **Validates: Requirements 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7**
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_php.h"
#include "htaccess_directive.h"
}

/* ------------------------------------------------------------------ */
/*  Constants: the exact PHP_INI_SYSTEM blacklist                      */
/* ------------------------------------------------------------------ */

static const std::vector<std::string> kSystemSettings = {
    "allow_url_fopen",
    "allow_url_include",
    "disable_classes",
    "disable_functions",
    "engine",
    "expose_php",
    "open_basedir",
    "realpath_cache_size",
    "realpath_cache_ttl",
    "upload_tmp_dir",
    "max_file_uploads",
    "sys_temp_dir",
};

static bool is_system_setting(const std::string &name)
{
    return std::find(kSystemSettings.begin(), kSystemSettings.end(), name)
           != kSystemSettings.end();
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_php_dir(const std::string &name,
                                          const std::string &value)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = DIR_PHP_VALUE;
    d->line_number = 1;
    d->name = strdup(name.c_str());
    d->value = strdup(value.c_str());
    d->next = nullptr;
    return d;
}

static void free_dir(htaccess_directive_t *d)
{
    if (!d) return;
    free(d->name);
    free(d->value);
    free(d);
}

/* ------------------------------------------------------------------ */
/*  Generators                                                         */
/* ------------------------------------------------------------------ */

namespace gen {

/**
 * Generate a PHP ini setting name.
 * Mix of known system settings, known PERDIR settings, and random names.
 */
inline rc::Gen<std::string> phpSettingName()
{
    /* Known PHP_INI_PERDIR settings (should be accepted) */
    static const std::vector<std::string> kPerDirSettings = {
        "memory_limit", "max_input_time", "post_max_size",
        "upload_max_filesize", "safe_mode", "display_errors",
        "error_reporting", "max_execution_time", "session.gc_maxlifetime",
        "date.timezone", "default_charset", "log_errors",
    };

    /* Random alphanumeric setting name generator */
    static const std::string kNameChars =
        "abcdefghijklmnopqrstuvwxyz_0123456789.";

    auto randomName = rc::gen::mapcat(
        rc::gen::inRange(2, 25),
        [](int len) {
            return rc::gen::map(
                rc::gen::container<std::vector<char>>(
                    (std::size_t)len,
                    rc::gen::elementOf(kNameChars)),
                [](const std::vector<char> &v) {
                    std::string s(v.begin(), v.end());
                    /* Ensure starts with a letter */
                    if (!std::isalpha((unsigned char)s[0]))
                        s[0] = 'x';
                    return s;
                });
        });

    return rc::gen::oneOf(
        rc::gen::elementOf(kSystemSettings),
        rc::gen::elementOf(kPerDirSettings),
        randomName
    );
}

/** Generate a simple PHP ini value string. */
inline rc::Gen<std::string> phpValue()
{
    static const std::string kValueChars =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.MKG";
    return rc::gen::mapcat(
        rc::gen::inRange(1, 16),
        [](int len) {
            return rc::gen::map(
                rc::gen::container<std::vector<char>>(
                    (std::size_t)len,
                    rc::gen::elementOf(kValueChars)),
                [](const std::vector<char> &v) {
                    return std::string(v.begin(), v.end());
                });
        });
}

} /* namespace gen */

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class PhpBlacklistPropertyFixture : public ::testing::Test {
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
/*  Property 26: PHP 黑名单正确性                                      */
/*                                                                     */
/*  For any PHP setting name, exec_php_value() accepts it if and only  */
/*  if the name is NOT in php_ini_system_settings[].                   */
/*  System settings are silently ignored (return LSI_OK but no PHP ini */
/*  record). Non-system settings are passed through (PHP ini record    */
/*  created).                                                          */
/*                                                                     */
/*  **Validates: Requirements 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7**   */
/* ------------------------------------------------------------------ */

RC_GTEST_FIXTURE_PROP(PhpBlacklistPropertyFixture,
                      PhpValueAcceptsIffNotInSystemBlacklist,
                      ())
{
    auto settingName = *gen::phpSettingName();
    auto settingValue = *gen::phpValue();

    auto *dir = make_php_dir(settingName, settingValue);
    int rc = exec_php_value(session_.handle(), dir);

    /* exec_php_value always returns LSI_OK (even for blocked settings) */
    RC_ASSERT(rc == LSI_OK);

    const auto &records = session_.get_php_ini_records();

    if (is_system_setting(settingName)) {
        /* System settings should be REJECTED — no PHP ini record created */
        RC_ASSERT(records.empty());
    } else {
        /* Non-system settings should be ACCEPTED — PHP ini record created */
        RC_ASSERT(records.size() == 1u);
        RC_ASSERT(records[0].name == settingName);
        RC_ASSERT(records[0].value == settingValue);
        RC_ASSERT(records[0].is_admin == false);
    }

    free_dir(dir);
}
