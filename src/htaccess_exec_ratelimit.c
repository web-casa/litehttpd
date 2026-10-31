#include <unistd.h>
#include <stdio.h>
/**
 * htaccess_exec_ratelimit.c — Per-IP rate limiting via SHM counters.
 * Spec: docs/specs/rule-engine.md
 *
 * Uses the P4 SHM infrastructure (brute_force_record_t) to track request
 * counts per IP. When count exceeds N/M, returns LSI_ERROR (→ 403).
 * Optionally escalates to P4 BotList → P5 firewall on persistent violation.
 */
#include "htaccess_exec_ratelimit.h"
#include "htaccess_shm.h"
#include "ls.h"

#include <string.h>
#include <stdlib.h>

ratelimit_config_t ratelimit_parse(const char *value)
{
    ratelimit_config_t cfg = { .requests = 0, .window_secs = 60, .burst = 0, .action = 0 };
    if (!value) return cfg;

    /* Format: "N/M [burst=B] [action=deny|throttle|log]" */
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", value);

    char *slash = strchr(buf, '/');
    if (!slash) return cfg;
    *slash = '\0';
    cfg.requests = atoi(buf);
    cfg.window_secs = atoi(slash + 1);
    if (cfg.requests <= 0) cfg.requests = 100;
    if (cfg.window_secs <= 0) cfg.window_secs = 60;

    char *burst = strstr(slash + 1, "burst=");
    if (burst) cfg.burst = atoi(burst + 6);
    if (cfg.burst <= 0) cfg.burst = cfg.requests;

    if (strstr(slash + 1, "action=throttle")) cfg.action = 1;
    else if (strstr(slash + 1, "action=log")) cfg.action = 2;
    else cfg.action = 0;

    return cfg;
}

int exec_ratelimit(lsi_session_t *session, const htaccess_directive_t *dir)
{
    if (!session || !dir || !dir->value)
        return LSI_OK;

    ratelimit_config_t cfg = ratelimit_parse(dir->value);
    if (cfg.requests <= 0)
        return LSI_OK;

    /* Get client IP */
    char ip[64] = {0};
    int ip_len_tmp = 0; const char *ip_result = lsi_session_get_client_ip(session, &ip_len_tmp);
    if (ip_result) snprintf(ip, sizeof(ip), "%s", ip_result);
    int ip_len = ip_result ? (int)strlen(ip) : 0;
    if (ip_len <= 0)
        return LSI_OK; /* can't rate-limit without IP */

    /* SHM namespace key "r:<ip>" — brute-force protection keys "b:<ip>".
     * Sharing the bare IP made rate limiting feed the brute-force
     * counters (and vice versa). */
    char rl_key[52];
    {
        size_t keep = (size_t)ip_len > 46 ? 46 : (size_t)ip_len;
        rl_key[0] = 'r';
        rl_key[1] = ':';
        memcpy(rl_key + 2, ip, keep);
        rl_key[2 + keep] = '\0';
    }

    /* Get current counter from SHM */
    time_t now = time(NULL);
    brute_force_record_t *rec = shm_get_record(rl_key);
    if (!rec) {
        /* First request — create record */
        brute_force_record_t new_rec = {0};
        new_rec.attempt_count = 1;
        new_rec.first_attempt = now;
        shm_update_record(rl_key, &new_rec);
        return LSI_OK;
    }

    /* Check window */
    if (now - rec->first_attempt > cfg.window_secs) {
        /* Window expired — reset */
        rec->attempt_count = 1;
        rec->first_attempt = now;
        shm_update_record(rl_key, rec);
        return LSI_OK;
    }

    /* Within window — increment */
    brute_force_record_t out;
    shm_atomic_increment(rl_key, now, &out);
    int current = out.attempt_count;

    if (current > cfg.requests + cfg.burst) {
        /* Over limit */
        if (cfg.action == 0) {
            /* deny */
            lsi_log(session, LSI_LOG_WARN,
                    "[htaccess] RateLimit: %s exceeded %d/%ds (count=%d), denying",
                    ip, cfg.requests, cfg.window_secs, current);
            return LSI_ERROR;
        } else if (cfg.action == 1) {
            /* throttle: sleep proportional to excess */
            int excess = current - cfg.requests;
            int delay_ms = excess * 10; /* 10ms per excess request */
            if (delay_ms > 5000) delay_ms = 5000;
            usleep(delay_ms * 1000);
        }
        /* action == 2: log-only, fall through */
    }

    return LSI_OK;
}
