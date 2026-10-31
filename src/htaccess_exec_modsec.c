/**
 * htaccess_exec_modsec.c — ModSecurity bridge executor implementation.
 *
 * Wire contract (verified against OLS src/modules/modsecurity-ls/mod_security.cpp,
 * isBypassCheck()): modsecurity-ls reads the request env `modsecurity` at its
 * URI_MAP hook (LSI_HOOK_NORMAL) and, when it equals "off", returns without
 * creating the transaction — so no connection/URI/header/body rules run for
 * the request. The OLS cache module sets the same env when serving a cached
 * response, which makes it the supported per-request control surface.
 *
 * We only ever SET the variable (never clear it): a cache-served request that
 * already carries "off" keeps it, and directories without a SecRuleEngine
 * directive leave the engine in its configured state.
 */
#include "htaccess_exec_modsec.h"

#include <string.h>

int exec_modsec_bridge(lsi_session_t *session, const htaccess_directive_t *directives)
{
    if (!session || !directives)
        return LSI_OK;

    /* The merged list runs root-first, so the LAST SecRuleEngine is the
     * most-specific (deepest) one — Apache per-directory merge order.
     * Acting only on the final value prevents a root-level Off from
     * overriding a leaf-level On. */
    const htaccess_directive_t *effective = NULL;
    for (const htaccess_directive_t *dir = directives; dir; dir = dir->next) {
        if (dir->type == DIR_SEC_RULE_ENGINE && dir->value)
            effective = dir;
    }
    if (!effective)
        return LSI_OK;

    if (strcmp(effective->value, "off") == 0) {
        lsi_session_set_env(session, "modsecurity", 11, "off", 3);
        lsi_log(session, LSI_LOG_INFO,
                "[htaccess] SecRuleEngine Off: modsecurity bypassed for "
                "this request via env contract");
    } else if (strcmp(effective->value, "detectiononly") == 0) {
        /* libmodsecurity keeps the engine mode in the parsed rules set;
         * there is no per-transaction override in its API, so a
         * per-directory DetectionOnly cannot be honoured. Fail toward
         * protection (treat as On) and say so — silently claiming
         * detection-only would be worse than not supporting it. */
        lsi_log(session, LSI_LOG_WARN,
                "[htaccess] SecRuleEngine DetectionOnly is not supported "
                "by the OLS bridge; treating as On (rules enforced)");
    }
    /* "on" — nothing to do: an absent variable already means normal
     * processing, and clearing it could undo the cache module's own
     * bypass for cache-served responses. */
    return LSI_OK;
}
