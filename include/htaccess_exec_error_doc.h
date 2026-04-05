/**
 * htaccess_exec_error_doc.h - ErrorDocument directive executor
 *
 * Implements execution of ErrorDocument directives supporting three modes:
 * - External URL redirect (starts with http:// or https://): 302 redirect
 * - Quoted text message (starts with "): set response body directly
 * - Local file path (starts with /): sets OLS_ERROR_DOC_URI env hint
 *
 * Validates: Requirements 8.1, 8.2, 8.3, 8.4
 */
#ifndef HTACCESS_EXEC_ERROR_DOC_H
#define HTACCESS_EXEC_ERROR_DOC_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute an ErrorDocument directive (DIR_ERROR_DOCUMENT).
 *
 * Checks if the current response status matches dir->data.error_doc.error_code.
 * If it matches, applies the error document based on the value format:
 *
 * - External URL (http:// or https://): Sets status to 302 and Location header.
 * - Quoted text (starts with "): Strips quotes and sets response body.
 * - Local file path (starts with /): Sets OLS_ERROR_DOC_URI env hint.
 *   The actual handling is done in on_uri_map (mod_htaccess.c) where
 *   status changes take effect, not in exec_error_document directly.
 *
 * @param session  LSIAPI session handle.
 * @param dir      Directive with type DIR_ERROR_DOCUMENT.
 * @return 0 on success or no match, -1 on error.
 */
int exec_error_document(lsi_session_t *session, const htaccess_directive_t *dir);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_EXEC_ERROR_DOC_H */
