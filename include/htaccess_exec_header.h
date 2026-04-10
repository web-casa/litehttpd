/**
 * htaccess_exec_header.h - Header and RequestHeader directive executors
 *
 * Implements execution of Header (set/unset/append/merge/add) and
 * RequestHeader (set/unset) directives via LSIAPI session calls.
 *
 * Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7
 */
#ifndef HTACCESS_EXEC_HEADER_H
#define HTACCESS_EXEC_HEADER_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute a Header directive (set/unset/append/merge/add).
 *
 * @param session  LSIAPI session handle.
 * @param dir      Directive with type DIR_HEADER_SET, DIR_HEADER_UNSET,
 *                 DIR_HEADER_APPEND, DIR_HEADER_MERGE, or DIR_HEADER_ADD.
 * @return LSI_OK on success, LSI_ERROR on failure.
 */
int exec_header(lsi_session_t *session, const htaccess_directive_t *dir);

/**
 * Execute a RequestHeader directive (set/unset).
 *
 * @param session  LSIAPI session handle.
 * @param dir      Directive with type DIR_REQUEST_HEADER_SET or
 *                 DIR_REQUEST_HEADER_UNSET.
 * @return LSI_OK on success, LSI_ERROR on failure.
 */
int exec_request_header(lsi_session_t *session, const htaccess_directive_t *dir);

/**
 * Reset the header shadow table. Call at the start of each request
 * to clear stale values from the previous request.
 */
void exec_header_reset_shadow(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_EXEC_HEADER_H */
