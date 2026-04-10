/**
 * htaccess_exec_acl.h - Access control directive executor
 *
 * Implements Apache-compatible Order/Allow/Deny access control evaluation.
 * Supports Order Allow,Deny and Order Deny,Allow modes with CIDR matching.
 *
 * Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.5, 6.6
 */
#ifndef HTACCESS_EXEC_ACL_H
#define HTACCESS_EXEC_ACL_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute access control evaluation over a directive list.
 *
 * Scans the directive list for DIR_ORDER, DIR_ALLOW_FROM, and DIR_DENY_FROM
 * directives. Retrieves the client IP from the session and evaluates access
 * according to Apache ACL semantics:
 *
 *   Order Allow,Deny: Default DENY. Evaluate Allow rules, then Deny rules.
 *     - If IP matches any Allow and no Deny → allowed
 *     - Otherwise → denied
 *
 *   Order Deny,Allow: Default ALLOW. Evaluate Deny rules, then Allow rules.
 *     - If IP matches any Allow → allowed
 *     - If IP matches any Deny and no Allow → denied
 *     - Otherwise → allowed
 *
 * When access is denied, sets response status to 403 and returns LSI_ERROR.
 *
 * @param session     LSIAPI session handle.
 * @param directives  Head of the full directive linked list.
 * @return LSI_OK if access is allowed, LSI_ERROR if denied (403 set).
 */
int exec_access_control(lsi_session_t *session,
                        const htaccess_directive_t *directives);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_EXEC_ACL_H */
