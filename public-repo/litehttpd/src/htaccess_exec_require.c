/**
 * htaccess_exec_require.c - Apache 2.4 Require access control executor
 *
 * Evaluates Require directives: all granted/denied, ip, not ip, env.
 * Supports RequireAny (OR) and RequireAll (AND) container blocks.
 * When Require coexists with Order/Allow/Deny, Require takes precedence.
 * Supports both IPv4 and IPv6 addresses, and Apache-style prefix matching
 * (e.g. "Require ip 10" matches 10.0.0.0/8).
 *
 * Validates: Requirements 8.1-8.8
 */
#include "htaccess_exec_require.h"
#include "htaccess_cidr.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * Count dots in a string (for Apache-style prefix length derivation).
 */
static int count_dots(const char *s)
{
    int n = 0;
    for (; *s; s++)
        if (*s == '.')
            n++;
    return n;
}

/**
 * Check if client IP (v4) matches a space-separated list of CIDR ranges.
 * Supports Apache-style prefix matching: if a token has no '/', auto-derive
 * the prefix length from the number of dots:
 *   0 dots → /8, 1 dot → /16, 2 dots → /24, 3 dots → /32
 * Returns 1 if any CIDR matches, 0 otherwise.
 */
static int ip_in_cidr_list_v4(uint32_t client_ip, const char *cidr_list)
{
    if (!cidr_list)
        return 0;

    /* Work on a copy since we tokenize */
    char *buf = strdup(cidr_list);
    if (!buf)
        return 0;

    int matched = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(buf, " \t", &saveptr);
    while (tok) {
        /* If no slash, try Apache-style prefix derivation for IPv4 */
        if (!strchr(tok, '/') && !strchr(tok, ':') &&
            strcasecmp(tok, "all") != 0) {
            int dots = count_dots(tok);
            int prefix;
            switch (dots) {
            case 0: prefix = 8;  break;
            case 1: prefix = 16; break;
            case 2: prefix = 24; break;
            default: prefix = 32; break;
            }
            /* Pad incomplete address with .0 and append /prefix */
            char cidr_buf[64];
            int pad = 3 - dots;  /* number of ".0" to append */
            char padded[48];
            snprintf(padded, sizeof(padded), "%s", tok);
            for (int p = 0; p < pad; p++) {
                size_t plen = strlen(padded);
                if (plen + 2 >= sizeof(padded)) break;
                padded[plen] = '.';
                padded[plen + 1] = '0';
                padded[plen + 2] = '\0';
            }
            snprintf(cidr_buf, sizeof(cidr_buf), "%s/%d", padded, prefix);
            cidr_v4_t cidr;
            if (cidr_parse(cidr_buf, &cidr) == 0) {
                if (cidr_match(&cidr, client_ip)) {
                    matched = 1;
                    break;
                }
            }
        } else {
            cidr_v4_t cidr;
            if (cidr_parse(tok, &cidr) == 0) {
                if (cidr_match(&cidr, client_ip)) {
                    matched = 1;
                    break;
                }
            }
        }
        tok = strtok_r(NULL, " \t", &saveptr);
    }

    free(buf);
    return matched;
}

/**
 * Check if client IPv6 matches a space-separated list of CIDR ranges.
 * Returns 1 if any CIDR matches, 0 otherwise.
 */
static int ip_in_cidr_list_v6(const uint8_t client_v6[16], const char *cidr_list)
{
    if (!cidr_list)
        return 0;

    char *buf = strdup(cidr_list);
    if (!buf)
        return 0;

    int matched = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(buf, " \t", &saveptr);
    while (tok) {
        cidr_v6_t cidr;
        if (cidr_v6_parse(tok, &cidr) == 0) {
            if (cidr_v6_match(&cidr, client_v6)) {
                matched = 1;
                break;
            }
        }
        tok = strtok_r(NULL, " \t", &saveptr);
    }

    free(buf);
    return matched;
}

/**
 * Evaluate a single Require directive against a client IP.
 * Returns: 1 = grant, 0 = deny, -1 = not applicable (skip)
 */
static int eval_single_require(const htaccess_directive_t *dir,
                               uint32_t client_ip,
                               const uint8_t *client_v6,
                               int is_ipv6,
                               lsi_session_t *session,
                               int auth_ok)
{
    switch (dir->type) {
    case DIR_REQUIRE_ALL_GRANTED:
        return 1;
    case DIR_REQUIRE_ALL_DENIED:
        return 0;
    case DIR_REQUIRE_IP:
        if (is_ipv6)
            return ip_in_cidr_list_v6(client_v6, dir->value) ? 1 : 0;
        return ip_in_cidr_list_v4(client_ip, dir->value) ? 1 : 0;
    case DIR_REQUIRE_NOT_IP:
        if (is_ipv6)
            return ip_in_cidr_list_v6(client_v6, dir->value) ? 0 : 1;
        return ip_in_cidr_list_v4(client_ip, dir->value) ? 0 : 1;
    case DIR_REQUIRE_ENV:
        if (session && dir->name) {
            int len = 0;
            const char *val = lsi_session_get_env(session, dir->name,
                                                   (int)strlen(dir->name), &len);
            /* Env var is "set" if returned non-NULL (even if empty) */
            return (val != NULL) ? 1 : 0;
        }
        return 0;
    case DIR_REQUIRE_VALID_USER:
        /* Use pre-validated auth result */
        return auth_ok ? 1 : -2; /* -2 = defer if no credentials yet */
    default:
        return -1; /* Not a Require directive */
    }
}

/* Forward declarations for mutual recursion */
static int eval_require_any(const htaccess_directive_t *container,
                            uint32_t client_ip, const uint8_t *client_v6,
                            int is_ipv6, lsi_session_t *session, int auth_ok);
static int eval_require_all(const htaccess_directive_t *container,
                            uint32_t client_ip, const uint8_t *client_v6,
                            int is_ipv6, lsi_session_t *session, int auth_ok);

/**
 * Evaluate a RequireAny container (OR logic).
 * Access granted if at least one child grants.
 * Returns: 1 = grant, 0 = deny
 */
static int eval_require_any(const htaccess_directive_t *container,
                            uint32_t client_ip, const uint8_t *client_v6,
                            int is_ipv6, lsi_session_t *session, int auth_ok)
{
    const htaccess_directive_t *child;
    for (child = container->data.require_container.children; child; child = child->next) {
        /* Handle nested containers */
        if (child->type == DIR_REQUIRE_ANY_OPEN) {
            if (eval_require_any(child, client_ip, client_v6, is_ipv6, session, auth_ok))
                return 1;
            continue;
        }
        if (child->type == DIR_REQUIRE_ALL_OPEN) {
            if (eval_require_all(child, client_ip, client_v6, is_ipv6, session, auth_ok))
                return 1;
            continue;
        }
        int r = eval_single_require(child, client_ip, client_v6, is_ipv6, session, auth_ok);
        if (r == 1)
            return 1;
        /* r == -1 means unrecognized directive — treat as not granting
         * (for OR logic, continue to check other children) */
    }
    return 0;
}

/**
 * Evaluate a RequireAll container (AND logic).
 * Access granted only if all children grant.
 * Returns: 1 = grant, 0 = deny
 */
static int eval_require_all(const htaccess_directive_t *container,
                            uint32_t client_ip, const uint8_t *client_v6,
                            int is_ipv6, lsi_session_t *session, int auth_ok)
{
    const htaccess_directive_t *child;
    int recognized = 0;
    for (child = container->data.require_container.children; child; child = child->next) {
        if (child->type == DIR_REQUIRE_ANY_OPEN) {
            recognized++;
            if (!eval_require_any(child, client_ip, client_v6, is_ipv6, session, auth_ok))
                return 0;
            continue;
        }
        if (child->type == DIR_REQUIRE_ALL_OPEN) {
            recognized++;
            if (!eval_require_all(child, client_ip, client_v6, is_ipv6, session, auth_ok))
                return 0;
            continue;
        }
        int r = eval_single_require(child, client_ip, client_v6, is_ipv6, session, auth_ok);
        if (r == 0)
            return 0;
        if (r == 1)
            recognized++;
        /* r == -2 (valid-user deferred to auth) — skip, don't deny */
        if (r == -2)
            continue;
        /* r == -1 (unrecognized) — treat as deny for safety */
        if (r == -1)
            return 0;
    }
    /* Empty or no recognized children — deny */
    return recognized > 0 ? 1 : 0;
}

int exec_require(lsi_session_t *session,
                 const htaccess_directive_t *directives,
                 const char *client_ip,
                 int auth_ok)
{
    if (!session || !directives)
        return LSI_OK;

    /* Check if any Require directives exist */
    int has_require = 0;
    const htaccess_directive_t *dir;
    for (dir = directives; dir; dir = dir->next) {
        switch (dir->type) {
        case DIR_REQUIRE_ALL_GRANTED:
        case DIR_REQUIRE_ALL_DENIED:
        case DIR_REQUIRE_IP:
        case DIR_REQUIRE_NOT_IP:
        case DIR_REQUIRE_ANY_OPEN:
        case DIR_REQUIRE_ALL_OPEN:
        case DIR_REQUIRE_ENV:
        case DIR_REQUIRE_VALID_USER:
            has_require = 1;
            break;
        default:
            break;
        }
        if (has_require)
            break;
    }

    if (!has_require)
        return LSI_OK;

    /* If Require coexists with Order/Allow/Deny, log warning */
    for (dir = directives; dir; dir = dir->next) {
        if (dir->type == DIR_ORDER || dir->type == DIR_ALLOW_FROM ||
            dir->type == DIR_DENY_FROM) {
            lsi_log(session, LSI_LOG_WARN,
                    "[htaccess] Require and Order/Allow/Deny coexist; "
                    "Require takes precedence");
            break;
        }
    }

    /* Check if any Require directive actually needs client IP (recursive) */
    int needs_ip = 0;
    for (dir = directives; dir && !needs_ip; dir = dir->next) {
        if (dir->type == DIR_REQUIRE_IP || dir->type == DIR_REQUIRE_NOT_IP) {
            needs_ip = 1;
        } else if (dir->type == DIR_REQUIRE_ANY_OPEN || dir->type == DIR_REQUIRE_ALL_OPEN) {
            const htaccess_directive_t *child;
            for (child = dir->data.require_container.children; child && !needs_ip; child = child->next) {
                if (child->type == DIR_REQUIRE_IP || child->type == DIR_REQUIRE_NOT_IP) {
                    needs_ip = 1;
                } else if (child->type == DIR_REQUIRE_ANY_OPEN || child->type == DIR_REQUIRE_ALL_OPEN) {
                    /* Second level nesting */
                    const htaccess_directive_t *gc;
                    for (gc = child->data.require_container.children; gc; gc = gc->next) {
                        if (gc->type == DIR_REQUIRE_IP || gc->type == DIR_REQUIRE_NOT_IP) {
                            needs_ip = 1;
                            break;
                        }
                    }
                }
            }
        }
    }

    /* Parse client IP only when IP-based Require exists */
    uint32_t ip_val = 0;
    uint8_t  ip_v6[16] = {0};
    int is_ipv6 = 0;

    if (needs_ip) {
        if (client_ip && strchr(client_ip, ':')) {
            is_ipv6 = 1;
            if (ip_parse_v6(client_ip, ip_v6) != 0) {
                lsi_session_set_status(session, 403);
                return LSI_ERROR;
            }
        } else if (!client_ip || ip_parse(client_ip, &ip_val) != 0) {
            /* Cannot parse IP but IP-based Require exists — deny for safety */
            lsi_session_set_status(session, 403);
            return LSI_ERROR;
        }
    }

    /* Pre-scan for valid-user anywhere in the Require tree (including containers) */
    int has_valid_user_anywhere = 0;
    for (dir = directives; dir; dir = dir->next) {
        if (dir->type == DIR_REQUIRE_VALID_USER) {
            has_valid_user_anywhere = 1;
            break;
        }
        if (dir->type == DIR_REQUIRE_ANY_OPEN || dir->type == DIR_REQUIRE_ALL_OPEN) {
            const htaccess_directive_t *child;
            for (child = dir->data.require_container.children; child; child = child->next) {
                if (child->type == DIR_REQUIRE_VALID_USER) {
                    has_valid_user_anywhere = 1;
                    break;
                }
                if ((child->type == DIR_REQUIRE_ANY_OPEN ||
                     child->type == DIR_REQUIRE_ALL_OPEN)) {
                    const htaccess_directive_t *gc;
                    for (gc = child->data.require_container.children; gc; gc = gc->next) {
                        if (gc->type == DIR_REQUIRE_VALID_USER) {
                            has_valid_user_anywhere = 1;
                            break;
                        }
                    }
                }
                if (has_valid_user_anywhere) break;
            }
        }
        if (has_valid_user_anywhere) break;
    }

    /* Evaluate Require directives — implicit RequireAny (OR) at top level */
    int granted = 0;
    int has_valid_user = 0;
    for (dir = directives; dir; dir = dir->next) {
        if (dir->type == DIR_REQUIRE_VALID_USER) {
            has_valid_user = 1;
            if (auth_ok) { granted = 1; break; }
            continue; /* No credentials yet — defer to exec_auth_basic */
        }
        if (dir->type == DIR_REQUIRE_ANY_OPEN) {
            if (eval_require_any(dir, ip_val, ip_v6, is_ipv6, session, auth_ok)) {
                granted = 1;
                break;
            }
            continue;
        }
        if (dir->type == DIR_REQUIRE_ALL_OPEN) {
            if (eval_require_all(dir, ip_val, ip_v6, is_ipv6, session, auth_ok)) {
                granted = 1;
                break;
            }
            continue;
        }
        int r = eval_single_require(dir, ip_val, ip_v6, is_ipv6, session, auth_ok);
        if (r == 1) {
            granted = 1;
            break;
        }
    }

    if (granted)
        return LSI_OK;

    /* If Require valid-user exists anywhere (including containers) and
     * no other rule granted, defer to exec_auth_basic() for real validation */
    if (has_valid_user || has_valid_user_anywhere)
        return LSI_OK;

    /* No grant and no valid-user fallback — deny */
    lsi_session_set_status(session, 403);
    return LSI_ERROR;
}
