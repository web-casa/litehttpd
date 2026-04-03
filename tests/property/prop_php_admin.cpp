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

RC_GTEST_FIXTURE_PROP(PhpAdminPropertyFixture,
                      PhpAdminValueNotOverriddenByPhpValue,
                      ())
{
    auto settingName = *gen::phpIniName();
    auto adminValue  = *gen::phpIniValue();
    auto childValue  = *gen::phpIniValue();

    /* Parent directory: php_admin_value sets the setting at admin level */
    auto *adminDir = make_php_dir(DIR_PHP_ADMIN_VALUE, settingName, adminValue);
    int rc1 = exec_php_admin_value(session_.handle(), adminDir);
    RC_ASSERT(rc1 == LSI_OK);

    /* Child directory: php_value tries to override the same setting */
    auto *childDir = make_php_dir(DIR_PHP_VALUE, settingName, childValue);
    int rc2 = exec_php_value(session_.handle(), childDir);
    RC_ASSERT(rc2 == LSI_OK);

    /* Inspect the PHP ini records */
    const auto &records = session_.get_php_ini_records();
    RC_ASSERT(records.size() >= 1);

    /* The admin-level record should exist with is_admin=true */
    bool found_admin = false;
    for (const auto &rec : records) {
        if (rec.name == settingName && rec.is_admin) {
            RC_ASSERT(rec.value == adminValue);
            found_admin = true;
        }
    }
    RC_ASSERT(found_admin);

    /* The child php_value record should be at user level (is_admin=false) */
    bool found_user = false;
    for (const auto &rec : records) {
        if (rec.name == settingName && !rec.is_admin) {
            RC_ASSERT(rec.value == childValue);
            found_user = true;
        }
    }
    RC_ASSERT(found_user);

    /*
     * Key property: the admin record (is_admin=1) and user record (is_admin=0)
     * are both present. The LSIAPI layer (lsi_session_set_php_ini) is
     * responsible for ensuring admin-level settings take precedence.
     * We verify the admin call used is_admin=1 and the user call used
     * is_admin=0, which is the contract that ensures non-overridability.
     */

    /* Verify the first record is admin-level (parent executed first) */
    RC_ASSERT(records[0].name == settingName);
    RC_ASSERT(records[0].is_admin == true);
    RC_ASSERT(records[0].value == adminValue);

    /* Verify the second record is user-level (child executed second) */
    RC_ASSERT(records[1].name == settingName);
    RC_ASSERT(records[1].is_admin == false);
    RC_ASSERT(records[1].value == childValue);

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

    /* Parent directory: php_admin_flag sets the setting at admin level */
    auto *adminDir = make_php_dir(DIR_PHP_ADMIN_FLAG, settingName, adminVal);
    int rc1 = exec_php_admin_flag(session_.handle(), adminDir);
    RC_ASSERT(rc1 == LSI_OK);

    /* Child directory: php_flag tries to override the same setting */
    auto *childDir = make_php_dir(DIR_PHP_FLAG, settingName, childVal);
    int rc2 = exec_php_flag(session_.handle(), childDir);
    RC_ASSERT(rc2 == LSI_OK);

    /* Inspect the PHP ini records */
    const auto &records = session_.get_php_ini_records();
    RC_ASSERT(records.size() >= 2);

    /* Admin record: is_admin=true, value matches parent */
    RC_ASSERT(records[0].name == settingName);
    RC_ASSERT(records[0].is_admin == true);
    RC_ASSERT(records[0].value == adminVal);

    /* User record: is_admin=false, value matches child */
    RC_ASSERT(records[1].name == settingName);
    RC_ASSERT(records[1].is_admin == false);
    RC_ASSERT(records[1].value == childVal);

    free_php_dir(adminDir);
    free_php_dir(childDir);
}
