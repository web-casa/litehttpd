#ifndef HTACCESS_EXEC_CACHE_H
#define HTACCESS_EXEC_CACHE_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cache bridge configuration from .htaccess */
typedef struct {
    int enabled;          /* 0=off, 1=public, 2=private */
    int max_expire;       /* seconds, 0=use default */
    int default_expire;   /* seconds, default 3600 */
    int ignore_no_last_mod;
    int store_private;
    int store_no_store;
    int esi;              /* 1=append ",esi=on" (fragment-aware caching) */
} cache_bridge_config_t;

/* Parse CacheEnable/CacheDisable value */
int cache_bridge_parse_enable(const char *value);

/* Execute cache directives: set X-LiteSpeed-Cache-Control header.
 * Called from on_send_resp_header. */
int exec_cache_bridge(lsi_session_t *session,
                      const htaccess_directive_t *directives);

/* Build the X-LiteSpeed-Cache-Control header value from config */
void cache_bridge_build_header(const cache_bridge_config_t *cfg,
                               char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif
#endif
