#ifndef HTACCESS_EXEC_RATELIMIT_H
#define HTACCESS_EXEC_RATELIMIT_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Rate limit configuration parsed from directive */
typedef struct {
    int requests;        /* N requests */
    int window_secs;     /* per M seconds */
    int burst;           /* burst allowance (default = requests) */
    int action;          /* 0=deny(403), 1=throttle(delay), 2=log-only */
} ratelimit_config_t;

/* Execute rate limit check for this session. Called from URI_MAP.
 * Returns LSI_OK if within limit, LSI_ERROR if exceeded (caller sets 403). */
int exec_ratelimit(lsi_session_t *session, const htaccess_directive_t *dir);

/* Parse "N/M [burst=B] [action=A]" from directive value. */
ratelimit_config_t ratelimit_parse(const char *value);

#ifdef __cplusplus
}
#endif
#endif
