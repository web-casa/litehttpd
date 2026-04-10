/**
 * htaccess_cache.c - Hash table cache for parsed .htaccess files
 *
 * Dual-mode implementation:
 * 1) OLS native ls_hash (if available via dlsym) — matches CyberPanel
 * 2) Built-in djb2 hash table fallback (test environments / stock OLS)
 *
 * Both modes use mtime-based invalidation: a get() with different
 * mtime/size/inode returns a miss (-1).
 *
 * Threading model: OLS uses a single-threaded event loop per worker
 * process. Each worker has its own address space, so the cache (both
 * native ls_hash and fallback djb2) is per-worker and never accessed
 * concurrently by multiple threads. No synchronization is required.
 * The native ls_hash backend is also inherently per-worker since it
 * lives in process-local heap memory (unlike ls_shmhash which is
 * cross-process). The fallback djb2 hash uses module-static globals
 * that are likewise per-process.
 *
 * If OLS ever moves to a multi-threaded worker model, this cache
 * would need a mutex or rwlock around get/put/destroy operations.
 *
 * Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.6
 */
#include "htaccess_cache.h"
#include "ols_native_api.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Cache entry — shared by both modes                                  */
/* ------------------------------------------------------------------ */

/* (cache_entry_t defined in htaccess_cache.h) */

/* ------------------------------------------------------------------ */
/* Estimate memory usage                                               */
/* ------------------------------------------------------------------ */
static size_t estimate_directives_memory(const htaccess_directive_t *head)
{
    size_t total = 0;
    for (const htaccess_directive_t *d = head; d; d = d->next) {
        total += sizeof(htaccess_directive_t);
        if (d->name)  total += strlen(d->name) + 1;
        if (d->value) total += strlen(d->value) + 1;
        if (d->env_condition) total += strlen(d->env_condition) + 1;

        switch (d->type) {
        case DIR_REDIRECT:
        case DIR_REDIRECT_MATCH:
            if (d->data.redirect.pattern)
                total += strlen(d->data.redirect.pattern) + 1;
            break;
        case DIR_FILES_MATCH:
            if (d->data.files_match.pattern)
                total += strlen(d->data.files_match.pattern) + 1;
            total += estimate_directives_memory(d->data.files_match.children);
            break;
        case DIR_FILES:
            total += estimate_directives_memory(d->data.files.children);
            break;
        case DIR_IFMODULE:
            total += estimate_directives_memory(d->data.ifmodule.children);
            break;
        case DIR_REQUIRE_ANY_OPEN:
        case DIR_REQUIRE_ALL_OPEN:
            total += estimate_directives_memory(d->data.require_container.children);
            break;
        case DIR_LIMIT:
        case DIR_LIMIT_EXCEPT:
            if (d->data.limit.methods)
                total += strlen(d->data.limit.methods) + 1;
            total += estimate_directives_memory(d->data.limit.children);
            break;
        case DIR_SETENVIF:
        case DIR_SETENVIF_NOCASE:
        case DIR_BROWSER_MATCH:
            if (d->data.envif.attribute)
                total += strlen(d->data.envif.attribute) + 1;
            if (d->data.envif.pattern)
                total += strlen(d->data.envif.pattern) + 1;
            break;
        case DIR_HEADER_EDIT:
        case DIR_HEADER_EDIT_STAR:
        case DIR_HEADER_ALWAYS_EDIT:
        case DIR_HEADER_ALWAYS_EDIT_STAR:
            if (d->data.header_ext.edit_pattern)
                total += strlen(d->data.header_ext.edit_pattern) + 1;
            break;
        case DIR_REWRITE_COND:
            if (d->data.rewrite_cond.cond_pattern)
                total += strlen(d->data.rewrite_cond.cond_pattern) + 1;
            if (d->data.rewrite_cond.flags_raw)
                total += strlen(d->data.rewrite_cond.flags_raw) + 1;
            break;
        case DIR_REWRITE_RULE:
            if (d->data.rewrite_rule.pattern)
                total += strlen(d->data.rewrite_rule.pattern) + 1;
            if (d->data.rewrite_rule.flags_raw)
                total += strlen(d->data.rewrite_rule.flags_raw) + 1;
            /* Recursively estimate conditions */
            total += estimate_directives_memory(d->data.rewrite_rule.conditions);
            break;
        default:
            break;
        }
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* Free a single cache entry                                           */
/* ------------------------------------------------------------------ */
static void cache_entry_free(cache_entry_t *entry)
{
    if (!entry) return;
    free(entry->filepath);
    htaccess_directives_free(entry->directives);
    free(entry);
}

/* ================================================================== */
/*  MODE 1: OLS native ls_hash (preferred on real OLS)                 */
/* ================================================================== */

static ls_hash_t g_native_cache = NULL;
static cache_entry_t *g_native_entry_list = NULL;  /* side-list for cleanup */

static int native_cache_init(size_t initial_buckets)
{
    const ols_native_api_t *api = ols_native_api_get();
    if (!api || !ols_native_hash_available())
        return -1;

    g_native_cache = api->hash_new(
        (int)initial_buckets,
        api->hash_hfstring,
        api->hash_cmpstring);
    return g_native_cache ? 0 : -1;
}

static int native_cache_get(const char *filepath, time_t current_mtime,
                            off_t current_size, ino_t current_inode,
                            htaccess_directive_t **out_directives)
{
    const ols_native_api_t *api = ols_native_api_get();
    if (!g_native_cache || !api || !filepath || !out_directives) return -1;

    ls_hashentry_t entry = api->hash_find(g_native_cache, filepath);
    if (!entry) return -1;

    cache_entry_t *ce = (cache_entry_t *)api->hash_getdata(entry);
    if (!ce) return -1;

    if (ce->mtime == current_mtime &&
        ce->file_size == current_size &&
        ce->inode == current_inode) {
        *out_directives = ce->directives;
        return 0;
    }
    return -1;
}

static int native_cache_put(const char *filepath, time_t mtime,
                            off_t file_size, ino_t inode,
                            htaccess_directive_t *directives)
{
    const ols_native_api_t *api = ols_native_api_get();
    if (!g_native_cache || !api || !filepath) return -1;

    /* Check for existing entry */
    ls_hashentry_t existing = api->hash_find(g_native_cache, filepath);
    if (existing) {
        cache_entry_t *ce = (cache_entry_t *)api->hash_getdata(existing);
        if (ce) {
            htaccess_directives_free(ce->directives);
            ce->directives = directives;
            ce->mtime = mtime;
            ce->file_size = file_size;
            ce->inode = inode;
            ce->memory_usage = sizeof(cache_entry_t)
                             + strlen(filepath) + 1
                             + estimate_directives_memory(directives);
            return 0;
        }
    }

    /* New entry */
    cache_entry_t *ce = calloc(1, sizeof(cache_entry_t));
    if (!ce) return -1;

    ce->filepath = strdup(filepath);
    if (!ce->filepath) { free(ce); return -1; }
    ce->mtime = mtime;
    ce->file_size = file_size;
    ce->inode = inode;
    ce->directives = directives;
    ce->memory_usage = sizeof(cache_entry_t)
                     + strlen(filepath) + 1
                     + estimate_directives_memory(directives);

    ls_hashentry_t inserted = api->hash_insert(g_native_cache, ce->filepath, ce);
    if (!inserted) {
        cache_entry_free(ce);
        return -1;
    }
    /* Track for cleanup since ls_hash_delete doesn't free values */
    ce->chain_next = g_native_entry_list;
    g_native_entry_list = ce;
    return 0;
}

static void native_cache_destroy(void)
{
    /* Free all tracked entries before deleting the hash table */
    cache_entry_t *cur = g_native_entry_list;
    while (cur) {
        cache_entry_t *next = cur->chain_next;
        cache_entry_free(cur);
        cur = next;
    }
    g_native_entry_list = NULL;

    const ols_native_api_t *api = ols_native_api_get();
    if (g_native_cache && api && api->hash_delete) {
        api->hash_delete(g_native_cache);
    }
    g_native_cache = NULL;
}

/* ================================================================== */
/*  MODE 2: Built-in djb2 hash table (fallback)                        */
/* ================================================================== */

static htaccess_cache_t *g_cache = NULL;

static size_t hash_string(const char *str, size_t num_buckets)
{
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++) != 0)
        hash = ((hash << 5) + hash) + (unsigned long)c;
    return hash % num_buckets;
}

static int builtin_cache_init(size_t initial_buckets)
{
    if (initial_buckets == 0) initial_buckets = 64;

    htaccess_cache_t *cache = calloc(1, sizeof(htaccess_cache_t));
    if (!cache) return -1;

    cache->buckets = calloc(initial_buckets, sizeof(cache_entry_t *));
    if (!cache->buckets) { free(cache); return -1; }

    cache->num_buckets = initial_buckets;
    cache->num_entries = 0;
    g_cache = cache;
    return 0;
}

static int builtin_cache_get(const char *filepath, time_t current_mtime,
                             off_t current_size, ino_t current_inode,
                             htaccess_directive_t **out_directives)
{
    if (!g_cache || !filepath || !out_directives) return -1;

    size_t idx = hash_string(filepath, g_cache->num_buckets);
    cache_entry_t *entry = g_cache->buckets[idx];

    while (entry) {
        if (strcmp(entry->filepath, filepath) == 0) {
            if (entry->mtime == current_mtime &&
                entry->file_size == current_size &&
                entry->inode == current_inode) {
                *out_directives = entry->directives;
                return 0;
            }
            /* Stale: return miss. Eviction is deferred to cache_put
             * which safely replaces the entry under single ownership. */
            return -1;
        }
        entry = entry->chain_next;
    }
    return -1;
}

static int builtin_cache_put(const char *filepath, time_t mtime,
                             off_t file_size, ino_t inode,
                             htaccess_directive_t *directives)
{
    if (!g_cache || !filepath) return -1;

    size_t idx = hash_string(filepath, g_cache->num_buckets);
    cache_entry_t *entry = g_cache->buckets[idx];

    while (entry) {
        if (strcmp(entry->filepath, filepath) == 0) {
            htaccess_directives_free(entry->directives);
            entry->directives = directives;
            entry->mtime = mtime;
            entry->file_size = file_size;
            entry->inode = inode;
            entry->memory_usage = sizeof(cache_entry_t)
                                + strlen(filepath) + 1
                                + estimate_directives_memory(directives);
            return 0;
        }
        entry = entry->chain_next;
    }

    if (g_cache->num_entries >= CACHE_MAX_ENTRIES) return -1;

    cache_entry_t *new_entry = calloc(1, sizeof(cache_entry_t));
    if (!new_entry) return -1;

    new_entry->filepath = strdup(filepath);
    if (!new_entry->filepath) { free(new_entry); return -1; }

    new_entry->mtime = mtime;
    new_entry->file_size = file_size;
    new_entry->inode = inode;
    new_entry->directives = directives;
    new_entry->memory_usage = sizeof(cache_entry_t)
                            + strlen(filepath) + 1
                            + estimate_directives_memory(directives);

    new_entry->chain_next = g_cache->buckets[idx];
    g_cache->buckets[idx] = new_entry;
    g_cache->num_entries++;
    return 0;
}

static void builtin_cache_destroy(void)
{
    if (!g_cache) return;

    for (size_t i = 0; i < g_cache->num_buckets; i++) {
        cache_entry_t *entry = g_cache->buckets[i];
        while (entry) {
            cache_entry_t *next = entry->chain_next;
            cache_entry_free(entry);
            entry = next;
        }
    }
    free(g_cache->buckets);
    free(g_cache);
    g_cache = NULL;
}

/* ================================================================== */
/*  Public API — dispatches to native or built-in                      */
/* ================================================================== */

/* Track which mode is active */
static int g_use_native = 0;

int htaccess_cache_init(size_t initial_buckets)
{
    /* Try native ls_hash first */
    ols_native_api_init();
    if (ols_native_hash_available()) {
        if (native_cache_init(initial_buckets) == 0) {
            g_use_native = 1;
            return 0;
        }
    }
    /* Fallback to built-in */
    g_use_native = 0;
    return builtin_cache_init(initial_buckets);
}

int htaccess_cache_get(const char *filepath, time_t current_mtime,
                       off_t current_size, ino_t current_inode,
                       htaccess_directive_t **out_directives)
{
    if (g_use_native)
        return native_cache_get(filepath, current_mtime, current_size,
                                current_inode, out_directives);
    return builtin_cache_get(filepath, current_mtime, current_size,
                             current_inode, out_directives);
}

int htaccess_cache_put(const char *filepath, time_t mtime,
                       off_t file_size, ino_t inode,
                       htaccess_directive_t *directives)
{
    if (g_use_native)
        return native_cache_put(filepath, mtime, file_size, inode, directives);
    return builtin_cache_put(filepath, mtime, file_size, inode, directives);
}

void htaccess_cache_destroy(void)
{
    if (g_use_native)
        native_cache_destroy();
    else
        builtin_cache_destroy();
    g_use_native = 0;
}
