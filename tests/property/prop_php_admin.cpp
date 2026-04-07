/**
 * prop_php_admin.cpp - Property-based test for PHP admin non-overridable settings
 *
 * Feature: ols-htaccess-module
 *
 * Property 11: PHP admin 级别设置不可覆盖
 *
 * Verifies that parent directory php_admin_value is not overridden by
 * child directory php_value for the same setting name.
 *
 * **Validates: Requirements 5.3, 5.4**
 */
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <cstring>
#include <cstdlib>

#include "mock_lsiapi.h"
#include "gen_header.h"

extern "C" {
#include "htaccess_exec_php.h"
#include "htaccess_directive.h"
}

/* ------------------------------------------------------------------ */
/*  Helper: create a PHP directive                                     */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_php_dir(directive_type_t type,
                                          const std::string &name,
                                          const std::string &value)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = type;
    d->line_number = 1;
    d->name = strdup(name.c_str());
    d->value = strdup(value.c_str());
    d->next = nullptr;
    return d;
}

static void free_php_dir(htaccess_directive_t *d)
{
    if (!d) return;
    free(d->name);
    free(d->value);
    free(d);
}

/* ------------------------------------------------------------------ */
/*  Generator: PHP ini setting name (alphanumeric + underscores)       */
/* ------------------------------------------------------------------ */

namespace gen {

/**
 * Generate a PHP ini setting name that is NOT in the PHP_INI_SYSTEM list.
 * This ensures php_value/php_flag will actually be applied (not ignored).
 */
inline rc::Gen<std::string> phpIniName()
{
    /* Use common non-system PHP ini settings */
    return rc::gen::element<std::string>(
        "display_errors",
        "error_reporting",
        "max_execution_time",
        "date.timezone",
        "session.gc_maxlifetime",
        "session.save_path",
        "log_errors",
        "default_charset",
        "output_buffering",
        "short_open_tag"
    );
}

/**
 * Generate a PHP ini value (simple string).
 */
inline rc::Gen<std::string> phpIniValue()
{
    return rc::gen::element<std::string>(
        "1", "0", "on", "off",
        "E_ALL", "128M", "300",
        "UTC", "Europe/London",
        "/tmp/sessions", "UTF-8"
    );
}

} /* namespace gen */

/* ------------------------------------------------------------------ */
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class PhpAdminPropertyFixture : public ::testing::Test {
public:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }

    void TearDown() override {}

protected:
    MockSession session_;
};

/* ------------------------------------------------------------------ */
/*  Property 11: PHP admin 级别设置不可覆盖                            */
/*                                                                     */
/*  For any PHP setting name, if a parent directory sets               */
/*  php_admin_value/php_admin_flag, then a child directory's           */
/*  php_value/php_flag for the same setting should not override the    */
/*  admin-level setting. The admin value (is_admin=1) should be the    */
/*  last effective value.                                              */
/*                                                                     */
/*  Simulation: parent calls exec_php_admin_value, then child calls   */
/*  exec_php_value for the same setting. We verify the last PHP ini   */
/*  record with is_admin=1 has the parent's value.                    */
/*                                                                     */
/*  **Validates: Requirements 5.3, 5.4**                               */
/* ------------------------------------------------------------------ */

/* php_admin_value/flag are now blocked from .htaccess (security fix H7).
 * Verify they produce NO records when called from .htaccess context. */

RC_GTEST_FIXTURE_PROP(PhpAdminPropertyFixture,
                      PhpAdminValueNotOverriddenByPhpValue,
                      ())
{
    auto settingName = *gen::phpIniName();
    auto adminValue  = *gen::phpIniValue();
    auto childValue  = *gen::phpIniValue();

    /* php_admin_value is silently ignored in .htaccess */
    auto *adminDir = make_php_dir(DIR_PHP_ADMIN_VALUE, settingName, adminValue);
    int rc1 = exec_php_admin_value(session_.handle(), adminDir);
    RC_ASSERT(rc1 == LSI_OK);

    /* php_value still works normally */
    auto *childDir = make_php_dir(DIR_PHP_VALUE, settingName, childValue);
    int rc2 = exec_php_value(session_.handle(), childDir);
    RC_ASSERT(rc2 == LSI_OK);

    /* Only the php_value record should exist (admin was blocked) */
    const auto &records = session_.get_php_ini_records();
    RC_ASSERT(records.size() == 1);
    RC_ASSERT(records[0].name == settingName);
    RC_ASSERT(records[0].is_admin == false);
    RC_ASSERT(records[0].value == childValue);

    free_php_dir(adminDir);
    free_php_dir(childDir);
}

RC_GTEST_FIXTURE_PROP(PhpAdminPropertyFixture,
                      PhpAdminFlagNotOverriddenByPhpFlag,
                      ())
{
    auto settingName = *gen::phpIniName();
    auto adminFlag   = *rc::gen::arbitrary<bool>();
    auto childFlag   = *rc::gen::arbitrary<bool>();

    std::string adminVal = adminFlag ? "on" : "off";
    std::string childVal = childFlag ? "on" : "off";

    /* php_admin_flag is silently ignored in .htaccess */
    auto *adminDir = make_php_dir(DIR_PHP_ADMIN_FLAG, settingName, adminVal);
    int rc1 = exec_php_admin_flag(session_.handle(), adminDir);
    RC_ASSERT(rc1 == LSI_OK);

    /* php_flag still works normally */
    auto *childDir = make_php_dir(DIR_PHP_FLAG, settingName, childVal);
    int rc2 = exec_php_flag(session_.handle(), childDir);
    RC_ASSERT(rc2 == LSI_OK);

    /* Only the php_flag record should exist (admin was blocked) */
    const auto &records = session_.get_php_ini_records();
    RC_ASSERT(records.size() == 1);
    RC_ASSERT(records[0].name == settingName);
    RC_ASSERT(records[0].is_admin == false);
    RC_ASSERT(records[0].value == childVal);

    free_php_dir(adminDir);
    free_php_dir(childDir);
}
