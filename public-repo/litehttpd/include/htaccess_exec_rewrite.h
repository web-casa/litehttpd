/**
 * htaccess_exec_rewrite.h - RewriteRule/RewriteCond execution via OLS engine
 *
 * This module delegates rewrite execution to OLS's native RewriteEngine
 * via the parse_rewrite_rules/exec_rewrite_rules LSIAPI extension.
 * The module handles .htaccess parsing, text reconstruction, and caching;
 * OLS handles regex compilation, condition evaluation, and URI rewriting.
 *
 * Requires custom OLS with rewrite patch (patches/0002-lsiapi-rewrite.patch).
 * On stock OLS, returns -1 (unsupported) gracefully.
 */
#ifndef HTACCESS_EXEC_REWRITE_H
#define HTACCESS_EXEC_REWRITE_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute rewrite rules from the directive list via OLS rewrite engine.
 *
 * Scans directives for RewriteEngine On, collects RewriteBase/Cond/Rule,
 * rebuilds them as text, and passes to g_api->parse_rewrite_rules() +
 * g_api->exec_rewrite_rules().
 *
 * @param session     LSIAPI session handle
 * @param directives  Full directive list from htaccess_dirwalk()
 * @return  1 = URI changed or response sent (redirect/403/410)
 *          0 = no match or engine off
 *         -1 = error or unsupported (stock OLS)
 */
int exec_rewrite_rules(lsi_session_t *session,
                       const htaccess_directive_t *directives);

/**
 * Rebuild rewrite directive text from parsed directives.
 *
 * Produces a text buffer in the format that OLS RewriteEngine::parseRules()
 * expects (RewriteCond/RewriteRule lines separated by newlines).
 *
 * @param directives  Full directive list
 * @param out_len     Output: length of returned buffer
 * @return  malloc'd text buffer (caller must free), or NULL if no rewrite directives
 */
char *rebuild_rewrite_text(const htaccess_directive_t *directives, int *out_len);

/**
 * Free cached rewrite handle. Call on module cleanup.
 */
void rewrite_cache_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* HTACCESS_EXEC_REWRITE_H */
