/**
 * htaccess_exec_brute_force.h - Brute force protection executor
 *
 * Implements per-IP failed attempt tracking with configurable thresholds.
 * Supports block (403) and throttle (delay) actions.
 *
 * Validates: Requirements 12.1, 12.2, 12.3, 12.4, 12.5, 12.6, 12.7, 12.8
 */
#ifndef HTACCESS_EXEC_BRUTE_FORCE_H
#define HTACCESS_EXEC_BRUTE_FORCE_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Default values per Requirements 12.7 and 12.8 */
#define BF_DEFAULT_ALLOWED_ATTEMPTS  10
#define BF_DEFAULT_WINDOW_SEC        300
#define BF_DEFAULT_THROTTLE_MS       1000
#define BF_MAX_THROTTLE_MS           30000  /* Safety cap for throttle delay */

/**
 * Execute brute force protection over a directive list.
 *
 * Scans directives for brute force configuration:
 *   - BruteForceProtection On/Off
 *   - BruteForceAllowedAttempts N
 *   - BruteForceWindow W (seconds)
 *   - BruteForceAction block|throttle
 *   - BruteForceThrottleDuration ms
 *
 * If enabled, tracks per-IP failed attempts using shared memory.
 * When the threshold is exceeded within the window:
 *   - block: sets status 403 and returns LSI_ERROR
 *   - throttle: records throttle intent (in tests) or sleeps (production)
 *
 * If shared memory is not initialized or allocation fails, protection
 * is disabled and the request continues normally.
 *
 * @param session     LSIAPI session handle.
 * @param directives  Head of the directive linked list.
 * @param client_ip   Client IP address string.
 * @return LSI_OK if request should proceed, LSI_ERROR if blocked.
 */
/**
 * @param count_attempt  If non-zero, increment attempt counter (auth failure).
 *                       If zero, only check if IP is already blocked.
 */
int exec_brute_force(lsi_session_t *session,
                     const htaccess_directive_t *directives,
                     const char *client_ip,
                     int count_attempt);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_EXEC_BRUTE_FORCE_H */
