/**
 * htaccess_regex.h — Unified PCRE2 regex wrapper (tri-state, resource-limited)
 *
 * Spec: docs/specs/regex-engine.md
 * Replaces POSIX regcomp/regexec across executors. Key properties:
 *  - tri-state result: MATCH / NO_MATCH / ERROR (never conflate the last two)
 *  - match_limit / depth_limit applied via pcre2_match_context
 *  - thread-local compiled-pattern cache (LRU, per tag)
 *  - match offsets use the regmatch_t shape (so/ho are byte offsets, -1=unset)
 */
#ifndef HTACCESS_REGEX_H
#define HTACCESS_REGEX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HTRE_MATCH       1   /* pattern matched */
#define HTRE_NO_MATCH    0   /* pattern did not match */
#define HTRE_ERROR      (-1) /* internal error (bad subject, exec failure)  */
#define HTRE_HIT_LIMIT  (-2) /* resource limit hit — must NOT be ignored    */

#define HTRE_MAX_GROUPS 10   /* $0..$9 */

typedef struct {
    int so;                     /* start offset, -1 if unset */
    int eo;                     /* end offset, -1 if unset   */
} htre_match_t;                 /* regmatch_t-compatible shape */

typedef struct htre_regex htre_regex_t;   /* opaque compiled pattern */

/* Compile (or fetch from thread-local cache). tag identifies the caller
 * (sees docs/specs/regex-engine.md §5); pattern length capped at 512.
 * flags: HTRE_ICASE for case-insensitive. Captures are always enabled.
 * Returns NULL on compile error (errbuf receives the PCRE2 message). */
htre_regex_t *htre_get_cached(int tag, const char *pattern, int flags,
                              char *errbuf, size_t errlen);

/* Execute against subject (NUL-terminated, length capped at 8192).
 * On MATCH and nmatch>0, pmatch[0..nmatch-1] receive group offsets.
 * Returns HTRE_MATCH / HTRE_NO_MATCH / HTRE_ERROR / HTRE_HIT_LIMIT. */
int htre_exec(const htre_regex_t *re, const char *subject,
              size_t nmatch, htre_match_t *pmatch);

/* Release all cached patterns for this thread (worker atexit hook). */
void htre_thread_cleanup(void);

#ifdef __cplusplus
}
#endif
#endif
