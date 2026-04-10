/**
 * htaccess_exec_options.h - Options directive executor
 *
 * Implements execution of the Options directive. Parses +/-Indexes,
 * +/-FollowSymLinks, +/-MultiViews, +/-ExecCGI flags from the directive
 * value and calls lsi_session_set_dir_option() via LSIAPI.
 *
 * Unknown flags are logged at WARN level and ignored.
 *
 * Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5
 */
#ifndef HTACCESS_EXEC_OPTIONS_H
#define HTACCESS_EXEC_OPTIONS_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute an Options directive.
 *
 * Reads the tri-state flags from dir->data.options (indexes,
 * follow_symlinks, multiviews, exec_cgi) and applies them via
 * lsi_session_set_dir_option(). Each flag uses +1 = enable,
 * -1 = disable, 0 = unchanged.
 *
 * @param session  LSIAPI session handle.
 * @param dir      Directive with type DIR_OPTIONS.
 * @return LSI_OK on success, LSI_ERROR on failure.
 */
int exec_options(lsi_session_t *session, const htaccess_directive_t *dir);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_EXEC_OPTIONS_H */
