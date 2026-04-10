/**
 * htaccess_exec_require.h - Apache 2.4 Require access control executor
 *
 * Evaluates Require all granted/denied, Require ip, Require not ip,
 * RequireAny (OR), and RequireAll (AND) container blocks.
 *
 * When Require directives coexist with Order/Allow/Deny, Require takes
 * precedence (Apache 2.4 semantics).
 *
 * Validates: Requirements 8.1-8.8
 */
#ifndef HTACCESS_EXEC_REQUIRE_H
#define HTACCESS_EXEC_REQUIRE_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Evaluate Apache 2.4 Require access control directives.
 *
 * @param session     LSIAPI session handle.
 * @param directives  Head of the directive linked list.
 * @param client_ip   Client IP address string (dotted-decimal).
 * @param auth_ok     1 if Basic auth credentials are pre-validated, 0 otherwise.
 * @return LSI_OK (0) if access allowed, LSI_ERROR (-1) if denied (403 set).
 */
int exec_require(lsi_session_t *session,
                 const htaccess_directive_t *directives,
                 const char *client_ip,
                 int auth_ok);

#ifdef __cplusplus
}
#endif

#endif /* HTACCESS_EXEC_REQUIRE_H */
