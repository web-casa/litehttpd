/**
 * htaccess_rewrite_native.h — built-in RewriteRule/RewriteCond engine.
 *
 * Executes mod_rewrite semantics inside the module (no patched OLS
 * required), using the P1 PCRE2 tri-state engine. Runs when the OLS
 * binary lacks the litehttpd rewrite extension ABI; the patched-OLS
 * fast path in htaccess_exec_rewrite.c keeps precedence when present.
 *
 * Supported (Apache subset):
 *   RewriteRule pattern substitution [flags]
 *     flags: R[=301..308] L NC QSA F G END NE PT(accepted,no-op)
 *     pattern: PCRE2, leading '!' negates, '-' substitution = no-op
 *     substitution: $0..$9 groups, %1..%9 last-cond groups,
 *       %{VAR} via expr_expand_var, absolute http(s):// = redirect
 *   RewriteCond teststring condpattern [flags]
 *     flags: NC OR
 *     teststring: %{VAR}, literal
 *     condpattern: PCRE2, '-f' '-d' '-s' file tests (docroot-relative),
 *       leading '!' negates, '<' '>' '=' lexicographic forms
 *   RewriteBase /path — prepended to relative substitutions
 *
 * Semantics notes (documented divergences from Apache):
 *   - Rules run in file order, one pass, [L]/[END] stop the chain; we do
 *     NOT re-scan from the top after an internal rewrite (Apache's
 *     per-directory context restarts). Covers the dominant real-world
 *     pattern (single catch-all front controller, e.g. WordPress).
 *   - $N back-references inside RewriteCond teststrings refer to the
 *     most recently matched rule's groups (approximated as empty on the
 *     first rule — Apache itself only defines them after a match).
 */
#ifndef HTACCESS_REWRITE_NATIVE_H
#define HTACCESS_REWRITE_NATIVE_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/* tag for the shared PCRE2 pattern cache */
#define RW_HTRE_TAG 4 /* next free after env=1,redir=0,hdr=2,expr=3 */

/**
 * Execute rewrite rules natively.
 *
 * Returns:
 *   0                — no rule matched, request untouched
 *   1                — internal rewrite applied (URI changed)
 *   301..308         — external redirect status (Location set)
 *   403 / 410        — forbidden / gone ([F] / [G])
 */
int rewrite_native_exec(lsi_session_t *session,
                        const htaccess_directive_t *directives,
                        const char *rewrite_base);

#ifdef __cplusplus
}
#endif

#endif /* HTACCESS_REWRITE_NATIVE_H */
