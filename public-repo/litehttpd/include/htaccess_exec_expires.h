/**
 * htaccess_exec_expires.h - Expires directive executor
 *
 * Implements execution of ExpiresActive and ExpiresByType directives.
 * When ExpiresActive is On and a matching ExpiresByType directive is found,
 * sets the Expires and Cache-Control: max-age headers on the response.
 *
 * Validates: Requirements 10.1, 10.2, 10.3
 */
#ifndef HTACCESS_EXEC_EXPIRES_H
#define HTACCESS_EXEC_EXPIRES_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute Expires directives from a directive list.
 *
 * Scans the directive list for DIR_EXPIRES_ACTIVE and DIR_EXPIRES_BY_TYPE.
 * If ExpiresActive is Off (or not found), no headers are set.
 * For each DIR_EXPIRES_BY_TYPE whose MIME type matches content_type,
 * sets Cache-Control: max-age=N and Expires headers on the response.
 *
 * @param session       LSIAPI session handle.
 * @param directives    Head of the directive linked list to scan.
 * @param content_type  The response Content-Type (e.g., "text/html").
 * @return LSI_OK on success, LSI_ERROR on failure.
 */
int exec_expires(lsi_session_t *session, const htaccess_directive_t *directives,
                 const char *content_type);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_EXEC_EXPIRES_H */
