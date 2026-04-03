/**
 * test_php_blacklist_fix.cpp - Unit tests for php_value/php_flag blacklist fix
 *
 * Verifies that PHP_INI_PERDIR settings (memory_limit, max_input_time,
 * post_max_size, upload_max_filesize, safe_mode) are ACCEPTED by
 * exec_php_value() and exec_php_flag(), while true PHP_INI_SYSTEM settings
 * (disable_functions, expose_php, allow_url_fopen, etc.) are still REJECTED.
 *
 * Validates: Requirements 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7
 */
#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

#include "mock_lsiapi.h"

extern "C" {
#include "htaccess_exec_php.h"
#include "htaccess_directive.h"
}

/* ------------------------------------------------------------------ */
/*  Helper                                                             */
/* ------------------------------------------------------------------ */

static htaccess_directive_t *make_php_dir(directive_type_t type,
                                          const char *name,
                                          const char *value)
{
    auto *d = (htaccess_directive_t *)calloc(1, sizeof(htaccess_directive_t));
    d->type = type;
    d->line_number = 1;
    d->name = name ? strdup(name) : nullptr;
    d->value = value ? strdup(value) : nullptr;
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
/*  Test fixture                                                       */
/* ------------------------------------------------------------------ */

class PhpBlacklistFixTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mock_lsiapi::reset_global_state();
        session_.reset();
    }

    MockSession session_;
};

/* ------------------------------------------------------------------ */
/*  PHP_INI_PERDIR settings should be ACCEPTED (Bug fix)               */
/* ------------------------------------------------------------------ */

TEST_F(PhpBlacklistFixTest, MemoryLimitAccepted)
{
    auto *dir = make_php_dir(DIR_PHP_VALUE, "memory_limit", "256M");
    EXPECT_EQ(exec_php_value(session_.handle(), dir), LSI_OK);
    auto &records = session_.get_php_ini_records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].name, "memory_limit");
    EXPECT_EQ(records[0].value, "256M");
    EXPECT_FALSE(records[0].is_admin);
    free_dir(dir);
}

TEST_F(PhpBlacklistFixTest, MaxInputTimeAccepted)
{
    auto *dir = make_php_dir(DIR_PHP_VALUE, "max_input_time", "120");
    EXPECT_EQ(exec_php_value(session_.handle(), dir), LSI_OK);
    auto &records = session_.get_php_ini_records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].name, "max_input_time");
    EXPECT_EQ(records[0].value, "120");
    free_dir(dir);
}

TEST_F(PhpBlacklistFixTest, PostMaxSizeAccepted)
{
    auto *dir = make_php_dir(DIR_PHP_VALUE, "post_max_size", "64M");
    EXPECT_EQ(exec_php_value(session_.handle(), dir), LSI_OK);
    auto &records = session_.get_php_ini_records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].name, "post_max_size");
    free_dir(dir);
}

TEST_F(PhpBlacklistFixTest, UploadMaxFilesizeAccepted)
{
    auto *dir = make_php_dir(DIR_PHP_VALUE, "upload_max_filesize", "128M");
    EXPECT_EQ(exec_php_value(session_.handle(), dir), LSI_OK);
    auto &records = session_.get_php_ini_records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].name, "upload_max_filesize");
    free_dir(dir);
}

TEST_F(PhpBlacklistFixTest, SafeModeAccepted)
{
    auto *dir = make_php_dir(DIR_PHP_FLAG, "safe_mode", "Off");
    EXPECT_EQ(exec_php_flag(session_.handle(), dir), LSI_OK);
    auto &records = session_.get_php_ini_records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].name, "safe_mode");
    EXPECT_EQ(records[0].value, "Off");
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  PHP_INI_SYSTEM settings should still be REJECTED                   */
/* ------------------------------------------------------------------ */

TEST_F(PhpBlacklistFixTest, DisableFunctionsRejected)
{
    auto *dir = make_php_dir(DIR_PHP_VALUE, "disable_functions", "exec,system");
    EXPECT_EQ(exec_php_value(session_.handle(), dir), LSI_OK);
    /* Should NOT have set any PHP ini — it was silently ignored */
    EXPECT_TRUE(session_.get_php_ini_records().empty());
    free_dir(dir);
}

TEST_F(PhpBlacklistFixTest, ExposePhpRejected)
{
    auto *dir = make_php_dir(DIR_PHP_FLAG, "expose_php", "Off");
    EXPECT_EQ(exec_php_flag(session_.handle(), dir), LSI_OK);
    EXPECT_TRUE(session_.get_php_ini_records().empty());
    free_dir(dir);
}

TEST_F(PhpBlacklistFixTest, AllowUrlFopenRejected)
{
    auto *dir = make_php_dir(DIR_PHP_VALUE, "allow_url_fopen", "1");
    EXPECT_EQ(exec_php_value(session_.handle(), dir), LSI_OK);
    EXPECT_TRUE(session_.get_php_ini_records().empty());
    free_dir(dir);
}

TEST_F(PhpBlacklistFixTest, AllowUrlIncludeRejected)
{
    auto *dir = make_php_dir(DIR_PHP_VALUE, "allow_url_include", "1");
    EXPECT_EQ(exec_php_value(session_.handle(), dir), LSI_OK);
    EXPECT_TRUE(session_.get_php_ini_records().empty());
    free_dir(dir);
}

TEST_F(PhpBlacklistFixTest, OpenBasedirRejected)
{
    auto *dir = make_php_dir(DIR_PHP_VALUE, "open_basedir", "/tmp");
    EXPECT_EQ(exec_php_value(session_.handle(), dir), LSI_OK);
    EXPECT_TRUE(session_.get_php_ini_records().empty());
    free_dir(dir);
}

TEST_F(PhpBlacklistFixTest, EngineRejected)
{
    auto *dir = make_php_dir(DIR_PHP_FLAG, "engine", "Off");
    EXPECT_EQ(exec_php_flag(session_.handle(), dir), LSI_OK);
    EXPECT_TRUE(session_.get_php_ini_records().empty());
    free_dir(dir);
}

TEST_F(PhpBlacklistFixTest, DisableClassesRejected)
{
    auto *dir = make_php_dir(DIR_PHP_VALUE, "disable_classes", "SomeClass");
    EXPECT_EQ(exec_php_value(session_.handle(), dir), LSI_OK);
    EXPECT_TRUE(session_.get_php_ini_records().empty());
    free_dir(dir);
}

/* ------------------------------------------------------------------ */
/*  php_admin_value/php_admin_flag CAN set system-level settings       */
/* ------------------------------------------------------------------ */

TEST_F(PhpBlacklistFixTest, AdminValueBypassesBlacklist)
{
    auto *dir = make_php_dir(DIR_PHP_ADMIN_VALUE, "disable_functions", "exec");
    EXPECT_EQ(exec_php_admin_value(session_.handle(), dir), LSI_OK);
    auto &records = session_.get_php_ini_records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].name, "disable_functions");
    EXPECT_TRUE(records[0].is_admin);
    free_dir(dir);
}
