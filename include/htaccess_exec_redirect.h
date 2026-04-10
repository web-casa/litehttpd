/**
 * htaccess_exec_redirect.h - Redirect and RedirectMatch directive executors
 *
 * Implements execution of Redirect (prefix match, optional status code)
 * and RedirectMatch (regex match with $N backreference substitution).
 * Both return 1 when a redirect is triggered (short-circuit), 0 otherwise.
 *
 * Validates: Requirements 7.1, 7.2, 7.3, 7.4, 7.5
 */
#ifndef HTACCESS_EXEC_REDIRECT_H
#define HTACCESS_EXEC_REDIRECT_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute a Redirect directive (DIR_REDIRECT).
 *
 * Checks if the request URI starts with dir->name (prefix match).
 * On match, sets the response status to dir->data.redirect.status_code
 * (defaulting to 302 if 0) and sets the Location header to dir->value.
 *
 * @param session  LSIAPI session handle.
 * @param dir      Directive with type DIR_REDIRECT.
 * @return 1 if redirect was triggered (short-circuit), 0 if no match,
 *         -1 on error.
 */
int exec_redirect(lsi_session_t *session, const htaccess_directive_t *dir);

/**
 * Execute a RedirectMatch directive (DIR_REDIRECT_MATCH).
 *
 * Compiles dir->data.redirect.pattern as a POSIX extended regex and
 * matches it against the request URI. On match, substitutes $N
 * backreferences in dir->value with captured groups and sets the
 * Location header and response status.
 *
 * @param session  LSIAPI session handle.
 * @param dir      Directive with type DIR_REDIRECT_MATCH.
 * @return 1 if redirect was triggered (short-circuit), 0 if no match,
 *         -1 on error.
 */
int exec_redirect_match(lsi_session_t *session, const htaccess_directive_t *dir);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_EXEC_REDIRECT_H */
