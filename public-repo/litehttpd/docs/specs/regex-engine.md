# Spec: unified regex wrapper (htre_*)

## Problem
Four executors use POSIX regcomp/regexec with divergent resource limits and a
shared correctness defect: any regexec error is treated as "no match", so a
resource-limit hit silently disables a deny rule (fail-open).

## Requirements
1. Single PCRE2-based wrapper used by env/expr/redirect/header (FilesMatch
   migrates onto the same limits in a second step).
2. Tri-state result: MATCH / NO_MATCH / ERROR. ERROR is *never* treated as
   NO_MATCH by callers.
3. Per-call policy at call sites: deny-rules treat ERROR as deny; enable
   rules log a warning and skip the rule.
4. Resource limits, applied uniformly via pcre2_match_context:
   match_limit=100000, depth_limit=1000 (matches existing files_match.c:110
   baseline), pattern length cap 512, subject cap 8192.
5. Thread-local compiled-pattern cache (8 slots LRU per executor tag) to
   avoid per-request compilation.
6. Flags: HTRE_CASELESS ([NC]); captures exposed as ovector up to 10 groups.

## References
- PCRE2 docs (pcre2_match_context, match_limit semantics)
- Apache mod_rewrite flags documentation ([NC]/[F]/[L]) — behavior reference
- Observation: existing thread-local caches in htaccess_exec_redirect.c:27

## Semantics change note (documented compat break)
POSIX ERE is leftmost-longest; PCRE2 is leftmost-first. A differential
corpus (tests/regression/dual_engine/) records patterns where this matters;
each is either accepted as PCRE2 behavior or pattern-normalized in parser.
