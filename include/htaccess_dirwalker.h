/**
 * htaccess_dirwalker.h - Directory hierarchy traversal for OLS .htaccess module
 *
 * Walks from the document root to the target directory, collecting and
 * merging .htaccess directives at each level. Child directory directives
 * of the same type override parent directory directives.
 *
 * Validates: Requirements 13.1, 13.2, 13.3
 */
#ifndef HTACCESS_DIRWALKER_H
#define HTACCESS_DIRWALKER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "htaccess_directive.h"
#include "ls.h"

/**
 * Collect and merge .htaccess directives from doc_root to target_dir.
 *
 * Traverses each directory level from doc_root down to target_dir.
 * At each level, constructs the .htaccess file path and checks the cache.
 * If cached, uses the cached directives; if not, attempts to read and
 * parse the file (stat failure is silently skipped).
 *
 * Merge semantics: walk from root to target (parent first, child last).
 * For each directive type, the LAST occurrence wins (child overrides parent).
 * Non-overlapping directive types from different levels are all preserved.
 *
 * @param session     LSIAPI session handle (may be NULL in tests).
 * @param doc_root    Document root path (e.g. "/var/www/html").
 * @param target_dir  Target directory path (must start with doc_root).
 * @return Merged directive linked list (caller must free with
 *         htaccess_directives_free()), or NULL if no directives found.
 */
htaccess_directive_t *htaccess_dirwalk(lsi_session_t *session,
                                       const char *doc_root,
                                       const char *target_dir);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_DIRWALKER_H */
