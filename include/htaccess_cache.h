/**
 * htaccess_cache.h - Hash table cache for parsed .htaccess files
 *
 * Provides a hash table keyed by file absolute path with mtime-based
 * invalidation.
 *
 * Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.6
 */
#ifndef HTACCESS_CACHE_H
#define HTACCESS_CACHE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "htaccess_directive.h"
#include <stddef.h>
#include <time.h>
#include <sys/types.h>

/** Maximum memory per cache entry in bytes (advisory, not enforced). */
#define CACHE_MAX_ENTRY_BYTES 8192

/** Maximum number of cache entries. */
#define CACHE_MAX_ENTRIES 4096

/**
 * Cache entry — one per cached .htaccess file.
 * Stored in a singly-linked chain for hash collision resolution.
 */
typedef struct cache_entry {
    char *filepath;                   /* Absolute path (hash key) */
    time_t mtime;                     /* File modification time */
    off_t  file_size;                 /* File size for invalidation */
    ino_t  inode;                     /* Inode number for invalidation */
    htaccess_directive_t *directives; /* Parsed directive list (owned) */
    size_t memory_usage;              /* Estimated memory for this entry */
    struct cache_entry *chain_next;   /* Next entry in same bucket */
} cache_entry_t;

/**
 * Hash table structure.
 */
typedef struct {
    cache_entry_t **buckets;  /* Array of bucket head pointers */
    size_t num_buckets;       /* Number of buckets */
    size_t num_entries;       /* Current number of stored entries */
} htaccess_cache_t;

/**
 * Initialise the global cache.
 *
 * @param initial_buckets  Number of hash buckets to allocate.
 * @return 0 on success, -1 on allocation failure.
 */
int htaccess_cache_init(size_t initial_buckets);

/**
 * Look up a cached entry.
 *
 * Uses mtime + file_size + inode for invalidation to prevent stale cache
 * hits when a file is modified within the same second.
 *
 * @param filepath        Absolute path of the .htaccess file.
 * @param current_mtime   Current modification time of the file on disk.
 * @param current_size    Current file size on disk.
 * @param current_inode   Current inode number on disk.
 * @param out_directives  On cache hit, set to the cached directive list.
 *                        Caller must NOT free this pointer.
 * @return 0 on hit, -1 on miss or metadata mismatch.
 */
int htaccess_cache_get(const char *filepath, time_t current_mtime,
                       off_t current_size, ino_t current_inode,
                       htaccess_directive_t **out_directives);

/**
 * Store or replace a cache entry.
 *
 * Takes ownership of @p directives — the cache will free them when the
 * entry is evicted or the cache is destroyed.  If an entry for the same
 * path already exists, the old directives are freed and replaced.
 *
 * @param filepath    Absolute path of the .htaccess file.
 * @param mtime       Modification time to associate with this entry.
 * @param file_size   File size to associate with this entry.
 * @param inode       Inode number to associate with this entry.
 * @param directives  Parsed directive list (ownership transferred).
 * @return 0 on success, -1 on allocation failure or cache full.
 */
int htaccess_cache_put(const char *filepath, time_t mtime,
                       off_t file_size, ino_t inode,
                       htaccess_directive_t *directives);

/**
 * Destroy the global cache, freeing all entries and the table itself.
 * Safe to call even if htaccess_cache_init() was never called.
 */
void htaccess_cache_destroy(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_CACHE_H */
