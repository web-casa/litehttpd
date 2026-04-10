/**
 * htaccess_exec_env.h - Environment variable directive executors
 *
 * Implements execution of SetEnv, SetEnvIf, and BrowserMatch directives.
 * SetEnv unconditionally sets an environment variable.
 * SetEnvIf conditionally sets a variable when a request attribute matches
 * a regex pattern (supports Remote_Addr, Request_URI, User-Agent, and
 * arbitrary request headers).
 * BrowserMatch is equivalent to SetEnvIf with an implicit User-Agent attribute.
 *
 * Validates: Requirements 11.1, 11.2, 11.3, 11.4, 11.5, 11.6
 */
#ifndef HTACCESS_EXEC_ENV_H
#define HTACCESS_EXEC_ENV_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute a SetEnv directive — unconditionally set an environment variable.
 *
 * @param session  LSIAPI session handle.
 * @param dir      Directive with type DIR_SETENV.
 *                 dir->name  = variable name
 *                 dir->value = variable value
 * @return LSI_OK on success, LSI_ERROR on failure.
 */
int exec_setenv(lsi_session_t *session, const htaccess_directive_t *dir);

/**
 * Execute a SetEnvIf directive — conditionally set an environment variable.
 *
 * Reads the attribute value from the session based on dir->data.envif.attribute:
 *   "Remote_Addr" -> client IP
 *   "Request_URI" -> request URI
 *   "User-Agent"  -> User-Agent request header
 *   other         -> request header by name
 *
 * Compiles dir->data.envif.pattern as a POSIX extended regex and matches
 * against the attribute value. If matched, sets the environment variable.
 *
 * @param session  LSIAPI session handle.
 * @param dir      Directive with type DIR_SETENVIF.
 *                 dir->name              = variable name to set
 *                 dir->value             = variable value to set
 *                 dir->data.envif.attribute = attribute to match
 *                 dir->data.envif.pattern   = regex pattern
 * @return LSI_OK on success, LSI_ERROR on failure.
 */
int exec_setenvif(lsi_session_t *session, const htaccess_directive_t *dir);

/**
 * Execute a BrowserMatch directive — set env var when User-Agent matches.
 *
 * Equivalent to SetEnvIf with attribute implicitly set to "User-Agent".
 * dir->data.envif.attribute is ignored (may be NULL).
 *
 * @param session  LSIAPI session handle.
 * @param dir      Directive with type DIR_BROWSER_MATCH.
 *                 dir->name              = variable name to set
 *                 dir->value             = variable value to set
 *                 dir->data.envif.pattern = regex pattern for User-Agent
 * @return LSI_OK on success, LSI_ERROR on failure.
 */
int exec_browser_match(lsi_session_t *session, const htaccess_directive_t *dir);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_EXEC_ENV_H */
