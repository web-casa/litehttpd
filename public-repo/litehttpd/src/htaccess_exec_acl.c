/**
 * htaccess_exec_acl.c - Access control directive executor implementation
 *
 * Implements Apache-compatible Order/Allow/Deny access control evaluation
 * with CIDR matching and "all" keyword support.
 * Supports both IPv4 and IPv6 addresses.
 *
 * Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.5, 6.6
 */
#include "htaccess_exec_acl.h"
#include "htaccess_cidr.h"

#include <string.h>
#include <strings.h>

/**
 * Check if a client IP matches a single Allow/Deny rule value (IPv4).
 *
 * The rule value can be:
 *   - "all"           → matches any IP
 *   - "A.B.C.D"       → matches a single IP (/32)
 *   - "A.B.C.D/N"     → matches a CIDR range
 *
 * @param rule_value  The Allow/Deny rule string (e.g. "192.168.1.0/24").
 * @param client_ip   Client IP as uint32_t in host byte order.
 * @return 1 if the IP matches the rule, 0 otherwise.
 */
static int ip_matches_rule_v4(const char *rule_value, uint32_t client_ip)
{
    cidr_v4_t cidr;

    if (!rule_value)
        return 0;

    /* Handle "all" keyword explicitly */
    if (strcasecmp(rule_value, "all") == 0)
        return 1;

    /* Parse CIDR/IP and match */
    if (cidr_parse(rule_value, &cidr) != 0)
        return 0; /* Invalid rule — skip */

    return cidr_match(&cidr, client_ip);
}

/**
 * Check if an IPv6 client address matches a single Allow/Deny rule value.
 *
 * @param rule_value  The Allow/Deny rule string (IPv6 CIDR or "all").
 * @param client_v6   Client IPv6 address (16 bytes).
 * @return 1 if the IP matches the rule, 0 otherwise.
 */
static int ip_matches_rule_v6(const char *rule_value, const uint8_t client_v6[16])
{
    cidr_v6_t cidr;

    if (!rule_value)
        return 0;

    if (strcasecmp(rule_value, "all") == 0)
        return 1;

    if (cidr_v6_parse(rule_value, &cidr) != 0)
        return 0;

    return cidr_v6_match(&cidr, client_v6);
}

int exec_access_control(lsi_session_t *session,
                        const htaccess_directive_t *directives)
{
    const htaccess_directive_t *dir;
    acl_order_t order = ORDER_ALLOW_DENY; /* default */
    int have_order = 0;
    int allow_matched = 0;
    int deny_matched = 0;
    int ip_len = 0;
    const char *ip_str;
    int is_ipv6 = 0;

    /* IPv4 path */
    uint32_t client_ip = 0;
    /* IPv6 path */
    uint8_t  client_v6[16];

    if (!session || !directives)
        return LSI_OK;

    /* Find the Order directive (use the last one found) */
    for (dir = directives; dir; dir = dir->next) {
        if (dir->type == DIR_ORDER) {
            order = dir->data.acl.order;
            have_order = 1;
        }
    }

    /* If no Order directive and no Allow/Deny rules, nothing to do */
    if (!have_order) {
        int has_acl_rules = 0;
        for (dir = directives; dir; dir = dir->next) {
            if (dir->type == DIR_ALLOW_FROM || dir->type == DIR_DENY_FROM) {
                has_acl_rules = 1;
                break;
            }
        }
        if (!has_acl_rules)
            return LSI_OK;
    }

    /* Get client IP */
    ip_str = lsi_session_get_client_ip(session, &ip_len);
    if (!ip_str || ip_len <= 0)
        return LSI_OK; /* No client IP available, allow by default */

    /* Detect IPv6 by presence of colon */
    if (strchr(ip_str, ':')) {
        is_ipv6 = 1;
        if (ip_parse_v6(ip_str, client_v6) != 0) {
            /* Cannot parse IPv6 — apply default policy */
            if (order == ORDER_ALLOW_DENY) {
                lsi_session_set_status(session, 403);
                return LSI_ERROR;
            }
            return LSI_OK;
        }
        /* Detect IPv4-mapped IPv6 (::ffff:A.B.C.D) — extract embedded IPv4
         * so IPv4 ACL rules work for mapped clients */
        if (client_v6[0] == 0 && client_v6[1] == 0 && client_v6[2] == 0 &&
            client_v6[3] == 0 && client_v6[4] == 0 && client_v6[5] == 0 &&
            client_v6[6] == 0 && client_v6[7] == 0 && client_v6[8] == 0 &&
            client_v6[9] == 0 && client_v6[10] == 0xff && client_v6[11] == 0xff) {
            /* Extract IPv4 from bytes 12-15 */
            client_ip = ((uint32_t)client_v6[12] << 24) |
                        ((uint32_t)client_v6[13] << 16) |
                        ((uint32_t)client_v6[14] << 8) |
                        (uint32_t)client_v6[15];
            is_ipv6 = 0; /* Treat as IPv4 for ACL matching */
        }
    } else {
        if (ip_parse(ip_str, &client_ip) != 0) {
            /* Cannot parse client IP. Apply the Order's default policy. */
            if (order == ORDER_ALLOW_DENY) {
                lsi_session_set_status(session, 403);
                return LSI_ERROR;
            }
            return LSI_OK;
        }
    }

    /* Check all Allow and Deny rules */
    for (dir = directives; dir; dir = dir->next) {
        if (dir->type == DIR_ALLOW_FROM && dir->value) {
            int m = is_ipv6 ? ip_matches_rule_v6(dir->value, client_v6)
                            : ip_matches_rule_v4(dir->value, client_ip);
            if (m) allow_matched = 1;
        } else if (dir->type == DIR_DENY_FROM && dir->value) {
            int m = is_ipv6 ? ip_matches_rule_v6(dir->value, client_v6)
                            : ip_matches_rule_v4(dir->value, client_ip);
            if (m) deny_matched = 1;
        }
    }

    /* Evaluate according to Apache ACL semantics */
    int denied = 0;

    if (order == ORDER_ALLOW_DENY) {
        if (allow_matched && !deny_matched)
            denied = 0;
        else
            denied = 1;
    } else {
        if (deny_matched && !allow_matched)
            denied = 1;
        else
            denied = 0;
    }

    if (denied) {
        lsi_session_set_status(session, 403);
        return LSI_ERROR;
    }

    return LSI_OK;
}
