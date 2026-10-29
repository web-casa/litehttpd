/**
 * htaccess_exec_modsec.h — ModSecurity bridge executor.
 *
 * Bridges the .htaccess `SecRuleEngine` directive (an LSWS Enterprise
 * feature OpenLiteSpeed lacks) to the OLS modsecurity-ls module through
 * its documented per-request bypass contract: when the request
 * environment variable `modsecurity` holds "off", modsecurity-ls skips
 * the request entirely (no transaction, no rules, no body inspection).
 * The OLS cache module uses the same variable when serving from cache.
 */
#ifndef HTACCESS_EXEC_MODSEC_H
#define HTACCESS_EXEC_MODSEC_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Apply SecRuleEngine directives from a merged directive list.
 *
 *  - Off            → sets request env "modsecurity=off" (full bypass)
 *  - On             → no-op (absent env means normal processing)
 *  - DetectionOnly  → cannot be expressed per-request through libmodsecurity;
 *                     logged once and treated as On
 *
 * The env must be set BEFORE modsecurity-ls's URI_MAP hook (priority
 * LSI_HOOK_NORMAL) runs — see the early hook in mod_htaccess.c.
 *
 * Returns LSI_OK always: a bridge failure must never break request serving.
 */
int exec_modsec_bridge(lsi_session_t *session, const htaccess_directive_t *directives);

#ifdef __cplusplus
}
#endif

#endif /* HTACCESS_EXEC_MODSEC_H */
