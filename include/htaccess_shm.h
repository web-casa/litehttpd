/**
 * htaccess_shm.h - Shared memory management for brute force protection
 *
 * Provides IP tracking record storage for brute force protection.
 * This implementation uses an in-memory hash table for testability.
 * In production, the backing store would be /dev/shm/ols/.
 *
 * Uses file locks for inter-process synchronization (conceptually;
 * the in-memory implementation is single-process safe).
 *
 * Validates: Requirements 12.2, 12.3, 12.4
 */
#ifndef HTACCESS_SHM_H
#define HTACCESS_SHM_H

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * IP tracking record for brute force protection.
 */
typedef struct {
    char    ip[46];          /* IPv4 or IPv6 address string */
    int     attempt_count;   /* Failed attempt count */
    time_t  first_attempt;   /* Time of first attempt in current window */
    time_t  blocked_until;   /* Block expiry time (0 = not blocked) */
} brute_force_record_t;

/**
 * Initialize the shared memory region.
 *
 * @param shm_path     Path for shared memory (e.g. "/dev/shm/ols/").
 *                     Ignored in the in-memory implementation.
 * @param max_records  Maximum number of IP records to store.
 * @return 0 on success, -1 on failure.
 */
int shm_init(const char *shm_path, size_t max_records);

/**
 * Look up an IP record.
 *
 * @param ip  Client IP address string.
 * @return Pointer to the record if found, NULL otherwise.
 *         The pointer is valid until the next shm_update_record or shm_destroy call.
 */
brute_force_record_t *shm_get_record(const char *ip);

/**
 * Create or update an IP record.
 *
 * @param ip      Client IP address string.
 * @param record  Record data to store (copied into the store).
 * @return 0 on success, -1 on failure (e.g. store full).
 */
int shm_update_record(const char *ip, const brute_force_record_t *record);

/**
 * Remove all records whose window has expired.
 *
 * A record is considered expired when:
 *   now - first_attempt > some reasonable threshold (caller decides)
 * This function removes records where blocked_until < now AND
 * the record is not actively being tracked.
 *
 * @param now  Current time.
 * @return Number of records cleaned up.
 */
int shm_cleanup_expired(time_t now);

/**
 * Destroy the shared memory region and free all resources.
 */
void shm_destroy(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_SHM_H */
