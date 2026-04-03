/**
 * htaccess_exec_php.c - PHP configuration directive executors
 *
 * Implements execution of php_value, php_flag, php_admin_value, and
 * php_admin_flag directives via LSIAPI session calls.
 *
 * Type mapping (matches CyberPanel's custom OLS):
 *   php_value       -> PHP_INI_TYPE_VALUE (0)
 *   php_flag        -> PHP_INI_TYPE_FLAG (1)
 *   php_admin_value -> PHP_INI_TYPE_ADMIN_VALUE (2)
 *   php_admin_flag  -> PHP_INI_TYPE_ADMIN_FLAG (3)
 *
 * Validates: Requirements 5.1, 5.2, 5.3, 5.4, 5.5
 */
#include "htaccess_exec_php.h"
#include <string.h>
#include <strings.h>

/* ------------------------------------------------------------------ */
/*  PHP_INI_SYSTEM settings list                                       */
/*                                                                     */
/*  These settings can only be set in php.ini or httpd.conf, NOT via   */
/*  php_value/php_flag in .htaccess. If referenced by php_value or     */
/*  php_flag, we log a warning and ignore the directive.               */
/*  php_admin_value/php_admin_flag CAN set these.                      */
/* ------------------------------------------------------------------ */

static const char *php_ini_system_settings[] = {
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
    /* Extension loading — critical security risk */
    "extension",
    "extension_dir",
    "zend_extension",
    /* Additional dangerous settings */
    "sendmail_path",
    "mail.log",
    "error_log",
    "doc_root",
    "user_dir",
    "cgi.force_redirect",
    NULL
};

/**
 * Check if a PHP ini setting name is a PHP_INI_SYSTEM level setting.
 */
static int is_php_ini_system(const char *name)
{
    if (!name)
        return 0;

    for (int i = 0; php_ini_system_settings[i] != NULL; i++) {
        if (strcasecmp(name, php_ini_system_settings[i]) == 0)
            return 1;
    }
    return 0;
}

int exec_php_value(lsi_session_t *session, const htaccess_directive_t *dir)
{
    if (!session || !dir || !dir->name || !dir->value)
        return LSI_ERROR;

    /* PHP_INI_SYSTEM settings cannot be set via php_value */
    if (is_php_ini_system(dir->name)) {
        lsi_log(session, LSI_LOG_WARN,
                "php_value: setting '%s' is PHP_INI_SYSTEM level, ignored "
                "(line %d)",
                dir->name, dir->line_number);
        return LSI_OK;
    }

    return lsi_session_set_php_ini(session, dir->name, dir->value,
                                   PHP_INI_TYPE_VALUE);
}

int exec_php_flag(lsi_session_t *session, const htaccess_directive_t *dir)
{
    if (!session || !dir || !dir->name || !dir->value)
        return LSI_ERROR;

    /* PHP_INI_SYSTEM settings cannot be set via php_flag */
    if (is_php_ini_system(dir->name)) {
        lsi_log(session, LSI_LOG_WARN,
                "php_flag: setting '%s' is PHP_INI_SYSTEM level, ignored "
                "(line %d)",
                dir->name, dir->line_number);
        return LSI_OK;
    }

    return lsi_session_set_php_ini(session, dir->name, dir->value,
                                   PHP_INI_TYPE_FLAG);
}

int exec_php_admin_value(lsi_session_t *session, const htaccess_directive_t *dir)
{
    if (!session || !dir || !dir->name || !dir->value)
        return LSI_ERROR;

    return lsi_session_set_php_ini(session, dir->name, dir->value,
                                   PHP_INI_TYPE_ADMIN_VALUE);
}

int exec_php_admin_flag(lsi_session_t *session, const htaccess_directive_t *dir)
{
    if (!session || !dir || !dir->name || !dir->value)
        return LSI_ERROR;

    return lsi_session_set_php_ini(session, dir->name, dir->value,
                                   PHP_INI_TYPE_ADMIN_FLAG);
}
