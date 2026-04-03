/**
 * htaccess_exec_auth.h - AuthType Basic executor
 *
 * Collects AuthType, AuthName, AuthUserFile, Require valid-user from
 * the directive list and validates the Authorization header.
 *
 * Validates: Requirements 10.1-10.9
 */
#ifndef HTACCESS_EXEC_AUTH_H
#define HTACCESS_EXEC_AUTH_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute HTTP Basic authentication.
 *
 * @param session     LSIAPI session handle.
 * @param directives  Head of the directive linked list.
 * @return LSI_OK if auth passes or not required, LSI_ERROR if 401/500.
 */
int exec_auth_basic(lsi_session_t *session,
                    const htaccess_directive_t *directives);

/**
 * Pre-validate Basic auth credentials without sending 401 challenge.
 * Used to pass auth state into RequireAny/RequireAll evaluation.
 *
 * @param session     LSIAPI session handle.
 * @param directives  Head of the directive linked list.
 * @return 1 if valid credentials provided, 0 if not (no 401 sent).
 */
int check_auth_credentials(lsi_session_t *session,
                           const htaccess_directive_t *directives);

/**
 * Check a password against an htpasswd hash.
 * Supports: crypt, MD5 ($apr1$), bcrypt ($2y$).
 *
 * @param hash      The hash string from htpasswd file.
 * @param password  The plaintext password to check.
 * @return 1 if match, 0 if no match, -1 on error.
 */
int htpasswd_check(const char *hash, const char *password);

#ifdef __cplusplus
}
#endif

#endif /* HTACCESS_EXEC_AUTH_H */
