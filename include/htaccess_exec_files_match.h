/**
 * htaccess_exec_files_match.h - FilesMatch directive executor
 *
 * Implements execution of FilesMatch directives. When the filename matches
 * the regex pattern, nested directives are executed in original order.
 * When the filename does not match, all nested directives are skipped.
 *
 * Validates: Requirements 9.1, 9.2, 9.3
 */
#ifndef HTACCESS_EXEC_FILES_MATCH_H
#define HTACCESS_EXEC_FILES_MATCH_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute a FilesMatch directive (DIR_FILES_MATCH).
 *
 * Compiles dir->data.files_match.pattern as a POSIX extended regex and
 * matches it against the given filename. If the filename matches, iterates
 * dir->data.files_match.children and executes each nested directive
 * (dispatching by type). If no match, skips all children.
 *
 * @param session   LSIAPI session handle.
 * @param dir       Directive with type DIR_FILES_MATCH.
 * @param filename  The filename to match against the regex pattern.
 * @return LSI_OK on success, LSI_ERROR on error (e.g., invalid regex).
 */
int exec_files_match(lsi_session_t *session, const htaccess_directive_t *dir,
                     const char *filename);

/**
 * Check if a filename matches a FilesMatch regex pattern (using cached regex).
 *
 * @param pattern  POSIX extended regex pattern.
 * @param filename The filename to match.
 * @return 1 if matched, 0 if not matched, -1 on error.
 */
int fm_regex_matches(const char *pattern, const char *filename);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_EXEC_FILES_MATCH_H */
