# ModSecurity Bridge — `.htaccess` `SecRuleEngine` for OpenLiteSpeed

## Status
Implemented (v1). Unit-tested; end-to-end verification pending VM availability.

## Problem
LSWS Enterprise honours `SecRuleEngine` (and `SecRuleRemoveById`) in
`.htaccess`; OpenLiteSpeed does not — per-directory WAF control is only
reachable through vhost Context module overrides in the WebAdmin UI.
Every migration guide tells users to "just add SecRuleEngine Off to
.htaccess"; on OLS that directive is silently ignored.

## The contract
OLS's own `modsecurity-ls` module
(`src/modules/modsecurity-ls/mod_security.cpp`) exposes a per-request
bypass surface:

```c
static int isBypassCheck(lsi_session_t *session)
{
    char var[4] = {0};
    int len = g_api->get_req_env(session, "modsecurity", 11, var, sizeof(var));
    if (len == 3 && strncasecmp(var, "off", 3) == 0)
        return 1;
    return 0;
}
```

When the request env `modsecurity` equals `off`, `UriMapHook` returns
before creating the transaction: no connection/URI/header rules run, and
the request-scoped body/response hooks are never enabled. The OLS cache
module sets the same variable when serving from cache (`cache.cpp`), so
this is the supported per-request control path, not an accident.

## Bridge behaviour
| `.htaccess`            | Action                                                   |
|------------------------|----------------------------------------------------------|
| `SecRuleEngine Off`    | set request env `modsecurity=off` (full per-request bypass) |
| `SecRuleEngine On`     | no-op — absent env already means normal processing       |
| `SecRuleEngine DetectionOnly` | not expressible per-request through libmodsecurity; logged WARN, treated as On |

Merge order: the dirwalk's merged list runs root-first, so the **last**
directive in the list is the most specific — a leaf `On` beats a root
`Off`. The executor acts only on the final value.

The bridge only ever **sets** the variable and never clears it, so it
cannot clobber the cache module's own cache-serve bypass.

## Hook ordering (the critical constraint)
`modsecurity-ls` registers `UriMapHook` at `LSI_HKPT_URI_MAP` with
priority `LSI_HOOK_NORMAL` (10); lower priority runs first. Our main
pass runs at priority 100 — far too late. The bridge therefore registers
a dedicated early hook:

```
{LSI_HKPT_URI_MAP, on_uri_map_early_hook, LSI_HOOK_EARLY (0), LSI_FLAG_ENABLED}
```

The early hook is self-contained (walk → bridge → free). It deliberately
does **not** share its directive list with the priority-100 pass through
the per-request cache: the main pass's error paths free the list they
own, and dual ownership across ~15 exit paths is a use-after-free
waiting to happen. The cost is a second dirwalk, which is a validated
cache hit (mtime/size/inode) — no re-parse, only the merge deep-copy.

## Files
- `include/htaccess_directive.h` — `DIR_SEC_RULE_ENGINE` (89)
- `src/htaccess_parser.c` — `SecRuleEngine On|Off|DetectionOnly` (case-insensitive)
- `include/htaccess_exec_modsec.h` / `src/htaccess_exec_modsec.c` — executor
- `src/mod_htaccess.c` — early URI_MAP hook registration
- `tests/test_modsec_bridge.cpp` — pins the exact env contract and merge order

## Out of scope (documented honestly)
- `SecRuleRemoveById` — no per-request rule-exclusion surface in
  modsecurity-ls; would require rule-level config, not request state.
- `DetectionOnly` — engine mode lives in the parsed rules set;
  libmodsecurity has no per-transaction override.
