/**
 * htaccess_exec_php.h - PHP configuration directive executors
 *
 * Implements execution of php_value, php_flag, php_admin_value, and
 * php_admin_flag directives via LSIAPI session calls.
 *
 * - php_value/php_flag: user-level (is_admin=0), overridable
 * - php_admin_value/php_admin_flag: admin-level (is_admin=1), non-overridable
 * - PHP_INI_SYSTEM level settings referenced by php_value/php_flag are
 *   logged as warnings and ignored.
 *
 * Validates: Requirements 5.1, 5.2, 5.3, 5.4, 5.5
 */
#ifndef HTACCESS_EXEC_PHP_H
#define HTACCESS_EXEC_PHP_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute a php_value directive.
 *
 * Passes the PHP ini setting to the handler at user level (is_admin=0).
 * If the setting is a PHP_INI_SYSTEM level setting, logs a warning and
 * ignores the directive.
 *
 * @param session  LSIAPI session handle.
 * @param dir      Directive with type DIR_PHP_VALUE.
 * @return LSI_OK on success, LSI_ERROR on failure.
 */
int exec_php_value(lsi_session_t *session, const htaccess_directive_t *dir);

/**
 * Execute a php_flag directive.
 *
 * Passes the PHP ini boolean setting (on/off) to the handler at user level
 * (is_admin=0). If the setting is a PHP_INI_SYSTEM level setting, logs a
 * warning and ignores the directive.
 *
 * @param session  LSIAPI session handle.
 * @param dir      Directive with type DIR_PHP_FLAG.
 * @return LSI_OK on success, LSI_ERROR on failure.
 */
int exec_php_flag(lsi_session_t *session, const htaccess_directive_t *dir);

/**
 * Execute a php_admin_value directive.
 *
 * Passes the PHP ini setting to the handler at admin level (is_admin=1).
 * Admin-level settings cannot be overridden by php_value in subdirectories.
 *
 * @param session  LSIAPI session handle.
 * @param dir      Directive with type DIR_PHP_ADMIN_VALUE.
 * @return LSI_OK on success, LSI_ERROR on failure.
 */
int exec_php_admin_value(lsi_session_t *session, const htaccess_directive_t *dir);

/**
 * Execute a php_admin_flag directive.
 *
 * Passes the PHP ini boolean setting to the handler at admin level (is_admin=1).
 * Admin-level settings cannot be overridden by php_flag in subdirectories.
 *
 * @param session  LSIAPI session handle.
 * @param dir      Directive with type DIR_PHP_ADMIN_FLAG.
 * @return LSI_OK on success, LSI_ERROR on failure.
 */
int exec_php_admin_flag(lsi_session_t *session, const htaccess_directive_t *dir);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_EXEC_PHP_H */
