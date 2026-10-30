/**
 * htaccess_exec_cache.c — LSCache .htaccess bridge.
 * Sets X-LiteSpeed-Cache-Control header from .htaccess directives.
 * Spec: docs/specs/lscache-bridge.md
 *
 * The OLS cache module (already loaded alongside this module) reads this
 * header and handles actual caching. litehttpd only sets the header.
 */
#include "htaccess_exec_cache.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int cache_bridge_parse_enable(const char *value)
{
    if (!value)
        return 0;
    if (strcasecmp(value, "public") == 0)
        return 1;
    if (strcasecmp(value, "private") == 0)
        return 2;
    return 0; /* off */
}

void cache_bridge_build_header(const cache_bridge_config_t *cfg,
                               char *buf, size_t buf_size)
{
    if (!cfg || !buf || buf_size == 0)
        return;
    buf[0] = '\0';

    if (cfg->enabled == 0) {
        snprintf(buf, buf_size, "off");
        return;
    }

    const char *visibility = (cfg->enabled == 2) ? "private" : "public";
    /* Default to 1 hour if no expire specified (max-age=0 would disable caching) */
    int max_age = 3600;
    if (cfg->max_expire > 0)
        max_age = cfg->max_expire;
    else if (cfg->default_expire > 0)
        max_age = cfg->default_expire;

    snprintf(buf, buf_size, "%s,max-age=%d", visibility, max_age);
}

int exec_cache_bridge(lsi_session_t *session,
                      const htaccess_directive_t *directives)
{
    if (!session || !directives)
        return LSI_OK;

    /* Scan directives for cache-related ones */
    cache_bridge_config_t cfg = {
        .enabled = -1,  /* -1 = not set in .htaccess, don't override */
        .max_expire = 0,
        .default_expire = 0,
        .ignore_no_last_mod = 0,
        .store_private = 0,
        .store_no_store = 0,
    };

    for (const htaccess_directive_t *dir = directives; dir; dir = dir->next) {
        switch (dir->type) {
        case DIR_CACHE_ENABLE:
            if (dir->value)
                cfg.enabled = cache_bridge_parse_enable(dir->value);
            break;
        case DIR_CACHE_DISABLE:
            cfg.enabled = 0;
            break;
        case DIR_CACHE_MAX_EXPIRE:
            if (dir->value)
                cfg.max_expire = atoi(dir->value);
            break;
        case DIR_CACHE_DEFAULT_EXPIRE:
            if (dir->value)
                cfg.default_expire = atoi(dir->value);
            break;
        default:
            break;
        }
    }

    /* If no cache directives in .htaccess, don't set header */
    if (cfg.enabled == -1)
        return LSI_OK;

    /* Build and set the X-LiteSpeed-Cache-Control header */
    char cache_header[256];
    cache_bridge_build_header(&cfg, cache_header, sizeof(cache_header));

    lsi_session_add_resp_header(session, "X-LiteSpeed-Cache-Control", 24,
                                cache_header, (int)strlen(cache_header));

    lsi_log(session, LSI_LOG_DEBUG,
            "[htaccess] LSCache bridge: %s", cache_header);

    return LSI_OK;
}
