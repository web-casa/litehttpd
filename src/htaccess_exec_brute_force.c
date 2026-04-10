/**
 * htaccess_exec_brute_force.c - Brute force protection executor implementation
 *
 * Tracks per-IP failed login attempts and triggers block or throttle
 * actions when the configured threshold is exceeded within the time window.
 *
 * Validates: Requirements 12.1, 12.2, 12.3, 12.4, 12.5, 12.6, 12.7, 12.8
 */
#include "htaccess_exec_brute_force.h"
#include "htaccess_shm.h"
#include "htaccess_cidr.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/**
 * Extract the leftmost IP from an X-Forwarded-For header value.
 * Returns a strdup'd string or NULL.
 */
static char *extract_first_ip(const char *xff, int xff_len)
{
    if (!xff || xff_len <= 0)
        return NULL;
    /* Skip leading whitespace */
    const char *p = xff;
    const char *end = xff + xff_len;
    while (p < end && (*p == ' ' || *p == '\t'))
        p++;
    /* Find end of first IP (comma or end) */
    const char *start = p;
    while (p < end && *p != ',' && *p != ' ' && *p != '\t')
        p++;
    if (p == start)
        return NULL;
    return strndup(start, (size_t)(p - start));
}

/**
 * Check if an IP is in a whitelist CIDR list (space/comma separated).
 */
static int is_ip_whitelisted(const char *ip, const char *whitelist)
{
    if (!ip || !whitelist)
        return 0;

    cidr_v4_t cidr;
    uint32_t ip_addr;

    /* Parse the IP */
    cidr_v4_t tmp;
    if (cidr_parse(ip, &tmp) != 0)
        return 0;
    ip_addr = tmp.network;

    char *copy = strdup(whitelist);
    if (!copy) return 0;

    char *saveptr = NULL;
    char *tok = strtok_r(copy, " ,\t", &saveptr);
    while (tok) {
        if (cidr_parse(tok, &cidr) == 0) {
            if (cidr_match(&cidr, ip_addr)) {
                free(copy);
                return 1;
            }
        }
        tok = strtok_r(NULL, " ,\t", &saveptr);
    }
    free(copy);
    return 0;
}

/**
 * Check if a URI matches any of the protect paths.
 */
static int is_protected_path(const char *uri, const char **paths, int num_paths)
{
    if (!uri || !paths || num_paths <= 0)
        return 1; /* No paths configured = protect all */

    for (int i = 0; i < num_paths; i++) {
        if (!paths[i]) continue;
        size_t plen = strlen(paths[i]);
        if (strncmp(uri, paths[i], plen) == 0) {
            /* Path boundary check: next char must be \0, /, or ? */
            char next = uri[plen];
            if (next == '\0' || next == '/' || next == '?')
                return 1;
        }
    }
    return 0;
}

int exec_brute_force(lsi_session_t *session,
                     const htaccess_directive_t *directives,
                     const char *client_ip,
                     int count_attempt)
{
    const htaccess_directive_t *dir;
    int enabled = 0;
    int allowed_attempts = BF_DEFAULT_ALLOWED_ATTEMPTS;
    int window_sec = BF_DEFAULT_WINDOW_SEC;
    bf_action_t action = BF_ACTION_BLOCK;
    int throttle_ms = BF_DEFAULT_THROTTLE_MS;
    int use_xff = 0;
    const char *whitelist_cidrs = NULL;
    const char *protect_paths[32];
    int num_paths = 0;
    brute_force_record_t *rec;
    brute_force_record_t new_rec;
    time_t now;
    char *xff_ip = NULL;

    if (!session || !directives || !client_ip)
        return LSI_OK;

    /* Step 1: Scan directives for brute force configuration */
    for (dir = directives; dir; dir = dir->next) {
        switch (dir->type) {
        case DIR_BRUTE_FORCE_PROTECTION:
            enabled = dir->data.brute_force.enabled;
            break;
        case DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS:
            allowed_attempts = dir->data.brute_force.allowed_attempts;
            break;
        case DIR_BRUTE_FORCE_WINDOW:
            window_sec = dir->data.brute_force.window_sec;
            break;
        case DIR_BRUTE_FORCE_ACTION:
            action = dir->data.brute_force.action;
            break;
        case DIR_BRUTE_FORCE_THROTTLE_DURATION:
            throttle_ms = dir->data.brute_force.throttle_ms;
            break;
        case DIR_BRUTE_FORCE_X_FORWARDED_FOR:
            use_xff = dir->data.brute_force.enabled;
            break;
        case DIR_BRUTE_FORCE_WHITELIST:
            whitelist_cidrs = dir->value;
            break;
        case DIR_BRUTE_FORCE_PROTECT_PATH:
            if (num_paths < 32 && dir->value)
                protect_paths[num_paths++] = dir->value;
            break;
        default:
            break;
        }
    }

    /* Step 2: If not enabled, return immediately */
    if (!enabled)
        return LSI_OK;

    /* Step 2a: XFF processing — use X-Forwarded-For IP if enabled */
    const char *effective_ip = client_ip;
    if (use_xff) {
        int xff_len = 0;
        const char *xff = lsi_session_get_req_header_by_name(
            session, "X-Forwarded-For", 15, &xff_len);
        if (xff && xff_len > 0) {
            xff_ip = extract_first_ip(xff, xff_len);
            if (xff_ip)
                effective_ip = xff_ip;
        }
    }

    /* Step 2b: Whitelist check — whitelisted IPs bypass protection */
    if (whitelist_cidrs && is_ip_whitelisted(effective_ip, whitelist_cidrs)) {
        free(xff_ip);
        return LSI_OK;
    }

    /* Step 2c: Protect path check — only protect configured paths */
    if (num_paths > 0) {
        int uri_len = 0;
        const char *uri = lsi_session_get_uri(session, &uri_len);
        if (!is_protected_path(uri, protect_paths, num_paths)) {
            free(xff_ip);
            return LSI_OK;
        }
    }

    /* Step 2d: Method check — only count POST requests (login attempts).
     * GET requests still check if IP is blocked but don't increment count.
     * This matches CyberPanel behavior: bf_check_hook only counts POST. */
    int is_post = 0;
    {
        int method_len = 0;
        const char *method = lsi_session_get_method(session, &method_len);
        if (method && method_len == 4 &&
            (method[0] == 'P' || method[0] == 'p') &&
            (method[1] == 'O' || method[1] == 'o') &&
            (method[2] == 'S' || method[2] == 's') &&
            (method[3] == 'T' || method[3] == 't'))
            is_post = 1;
    }

    /* Step 3: Periodic cleanup of expired records to prevent SHM exhaustion.
     * Without this, stale IP records accumulate and eventually fill the table,
     * causing shm_update_record to fail and disabling protection. */
    now = time(NULL);
    shm_cleanup_expired(now);

    /* Step 4: Get current record from shared memory */
    rec = shm_get_record(effective_ip);

    if (rec) {
        /* Step 4: Record exists — check if within window */
        if ((now - rec->first_attempt) < (time_t)window_sec) {
            /* Within window */
            if (rec->attempt_count >= allowed_attempts) {
                /* Threshold exceeded — save count, increment, apply action */
                int cur_count = rec->attempt_count;
                new_rec = *rec;
                new_rec.attempt_count++;
                shm_update_record(effective_ip, &new_rec);

                if (action == BF_ACTION_BLOCK) {
                    lsi_session_set_status(session, 403);
                    free(xff_ip);
                    return LSI_ERROR;
                } else if (action == BF_ACTION_THROTTLE) {
                    /* BF_ACTION_THROTTLE: progressive delay + 429 */
                    int excess = cur_count - allowed_attempts + 1;
                    if (excess < 1) excess = 1;
                    long long delay = (long long)throttle_ms * excess;
                    if (delay > 30000) delay = 30000; /* cap at 30s */
                    int progressive_ms = (int)delay;
                    if (progressive_ms > BF_MAX_THROTTLE_MS)
                        progressive_ms = BF_MAX_THROTTLE_MS;
                    char ms_str[32];
                    snprintf(ms_str, sizeof(ms_str), "%d", progressive_ms);
                    lsi_session_set_env(session,
                                        "BF_THROTTLE_MS", 14,
                                        ms_str, (int)strlen(ms_str));
#ifndef HTACCESS_UNIT_TEST
                    usleep((useconds_t)progressive_ms * 1000);
#endif
                    lsi_session_set_status(session, 429);
                    free(xff_ip);
                    return LSI_ERROR;
                } else if (action == BF_ACTION_LOG) {
                    /* BF_ACTION_LOG: record attempt but allow request */
                    char log_str[256];
                    snprintf(log_str, sizeof(log_str),
                             "BruteForce: IP=%s attempts=%d (log mode)",
                             effective_ip, new_rec.attempt_count);
                    lsi_session_set_env(session, "BF_LOG", 6,
                                        log_str, (int)strlen(log_str));
                    free(xff_ip);
                    return LSI_OK;
                }
            }

            /* Increment attempt count (POST only) */
            if (count_attempt && is_post) {
                new_rec = *rec;
                new_rec.attempt_count++;
                shm_update_record(effective_ip, &new_rec);
            }
        } else {
            /* Window expired — reset record (only when counting) */
            if (count_attempt && is_post) {
                memset(&new_rec, 0, sizeof(new_rec));
                strncpy(new_rec.ip, effective_ip, sizeof(new_rec.ip) - 1);
                new_rec.ip[sizeof(new_rec.ip) - 1] = '\0';
                new_rec.attempt_count = 1;
                new_rec.first_attempt = now;
                new_rec.blocked_until = 0;
                shm_update_record(effective_ip, &new_rec);
            }
        }
    } else {
        /* Step 5: No record — create new one (only when counting) */
        if (count_attempt && is_post) {
            memset(&new_rec, 0, sizeof(new_rec));
            strncpy(new_rec.ip, effective_ip, sizeof(new_rec.ip) - 1);
            new_rec.ip[sizeof(new_rec.ip) - 1] = '\0';
            new_rec.attempt_count = 1;
            new_rec.first_attempt = now;
            new_rec.blocked_until = 0;

            if (shm_update_record(effective_ip, &new_rec) != 0) {
                lsi_log(session, LSI_LOG_ERROR,
                        "BruteForce: SHM allocation failed for IP %s, "
                        "disabling protection", effective_ip);
                free(xff_ip);
                return LSI_OK;
            }
        }
    }

    free(xff_ip);
    return LSI_OK;
}
