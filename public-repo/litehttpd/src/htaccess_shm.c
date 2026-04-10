/**
 * htaccess_shm.c - Shared memory management for brute force protection
 *
 * Dual-mode implementation:
 * 1) OLS native ls_shmhash (if available) — cross-process IP tracking
 *    matching CyberPanel's approach (SHM pool "BFProt", hash "IPQuota")
 * 2) Built-in in-memory hash table fallback (test/single-process)
 *
 * Validates: Requirements 12.2, 12.3, 12.4
 */
#include "htaccess_shm.h"
#include "ols_native_api.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/*  MODE 1: OLS native ls_shmhash (preferred — cross-process)          */
/* ================================================================== */

/* SHM pool and hash table names matching CyberPanel */
#define SHM_POOL_NAME  "BFProt"
#define SHM_HASH_NAME  "IPQuota"
#define SHM_POOL_SIZE  (1024 * 1024)  /* 1 MB */
#define SHM_HASH_SIZE  2048

static ls_shmpool_t g_shm_pool = NULL;
static ls_shmhash_t g_shm_ht = NULL;
static int g_use_native_shm = 0;

static int native_shm_init(size_t max_records)
{
    const ols_native_api_t *api = ols_native_api_get();
    if (!api || !ols_native_shm_available())
        return -1;

    g_shm_pool = api->shm_opengpool(SHM_POOL_NAME, SHM_POOL_SIZE);
    if (!g_shm_pool) return -1;

    g_shm_ht = api->shmhash_open(g_shm_pool, SHM_HASH_NAME,
                                  (int)(max_records * sizeof(brute_force_record_t)),
                                  SHM_HASH_SIZE);
    if (!g_shm_ht) {
        g_shm_pool = NULL;
        return -1;
    }
    return 0;
}

/* Thread-local copy buffer — avoids returning a pointer into SHM
 * that could be modified by another process after we release the lock. */
static __thread brute_force_record_t g_shm_get_buf;

static brute_force_record_t *native_shm_get(const char *ip)
{
    const ols_native_api_t *api = ols_native_api_get();
    if (!g_shm_ht || !api || !ip) return NULL;

    ls_shmoff_t val_off = 0;
    api->shmhash_lock(g_shm_ht);
    ls_shmoff_t off = api->shmhash_get(g_shm_ht, ip, (int)strlen(ip), &val_off);
    if (off <= 0 || val_off <= 0) {
        api->shmhash_unlock(g_shm_ht);
        return NULL;
    }
    brute_force_record_t *rec = (brute_force_record_t *)
        api->shmhash_off2ptr(g_shm_ht, val_off);
    if (rec)
        memcpy(&g_shm_get_buf, rec, sizeof(brute_force_record_t));
    api->shmhash_unlock(g_shm_ht);
    return rec ? &g_shm_get_buf : NULL;
}

static int native_shm_update(const char *ip, const brute_force_record_t *record)
{
    const ols_native_api_t *api = ols_native_api_get();
    if (!g_shm_ht || !api || !ip || !record) return -1;

    api->shmhash_lock(g_shm_ht);
    ls_shmoff_t val_off = 0;
    ls_shmoff_t off = api->shmhash_get(g_shm_ht, ip, (int)strlen(ip), &val_off);
    if (off > 0 && val_off > 0) {
        brute_force_record_t *rec = (brute_force_record_t *)
            api->shmhash_off2ptr(g_shm_ht, val_off);
        if (rec)
            memcpy(rec, record, sizeof(brute_force_record_t));
    }
    /* Note: if entry doesn't exist, ls_shmhash_get creates it */
    api->shmhash_unlock(g_shm_ht);
    return 0;
}

static void native_shm_destroy(void)
{
    /* SHM pool persists across process restarts; just release handles */
    g_shm_ht = NULL;
    g_shm_pool = NULL;
}

/* ================================================================== */
/*  MODE 2: Built-in in-memory hash table (fallback)                   */
/* ================================================================== */

#define SHM_SLOT_EMPTY     0
#define SHM_SLOT_OCCUPIED  1
#define SHM_SLOT_TOMBSTONE 2

typedef struct {
    brute_force_record_t *records;
    int                  *occupied;
    size_t                capacity;
    size_t                count;
} shm_store_t;

static shm_store_t *g_store = NULL;

static size_t hash_ip(const char *ip, size_t capacity)
{
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*ip++) != 0)
        hash = ((hash << 5) + hash) + (unsigned long)c;
    return hash % capacity;
}

static int builtin_shm_init(size_t max_records)
{
    if (g_store) { shm_destroy(); }

    g_store = (shm_store_t *)calloc(1, sizeof(shm_store_t));
    if (!g_store) return -1;

    g_store->records = (brute_force_record_t *)calloc(max_records,
                                                       sizeof(brute_force_record_t));
    if (!g_store->records) { free(g_store); g_store = NULL; return -1; }

    g_store->occupied = (int *)calloc(max_records, sizeof(int));
    if (!g_store->occupied) {
        free(g_store->records); free(g_store); g_store = NULL; return -1;
    }

    g_store->capacity = max_records;
    g_store->count = 0;
    return 0;
}

static brute_force_record_t *builtin_shm_get(const char *ip)
{
    if (!g_store || !ip) return NULL;

    size_t start = hash_ip(ip, g_store->capacity);
    size_t idx = start;

    do {
        if (g_store->occupied[idx] == SHM_SLOT_OCCUPIED &&
            strncmp(g_store->records[idx].ip, ip,
                    sizeof(g_store->records[idx].ip) - 1) == 0)
            return &g_store->records[idx];
        if (g_store->occupied[idx] == SHM_SLOT_EMPTY)
            return NULL;
        idx = (idx + 1) % g_store->capacity;
    } while (idx != start);
    return NULL;
}

static int builtin_shm_update(const char *ip, const brute_force_record_t *record)
{
    if (!g_store || !ip || !record) return -1;

    size_t start = hash_ip(ip, g_store->capacity);
    size_t idx = start;
    size_t first_tombstone = (size_t)-1;

    do {
        if (g_store->occupied[idx] == SHM_SLOT_OCCUPIED &&
            strncmp(g_store->records[idx].ip, ip,
                    sizeof(g_store->records[idx].ip) - 1) == 0) {
            memcpy(&g_store->records[idx], record, sizeof(brute_force_record_t));
            strncpy(g_store->records[idx].ip, ip, sizeof(g_store->records[idx].ip) - 1);
            g_store->records[idx].ip[sizeof(g_store->records[idx].ip) - 1] = '\0';
            return 0;
        }
        if (g_store->occupied[idx] == SHM_SLOT_TOMBSTONE && first_tombstone == (size_t)-1)
            first_tombstone = idx;
        if (g_store->occupied[idx] == SHM_SLOT_EMPTY) {
            size_t ins = (first_tombstone != (size_t)-1) ? first_tombstone : idx;
            memcpy(&g_store->records[ins], record, sizeof(brute_force_record_t));
            strncpy(g_store->records[ins].ip, ip, sizeof(g_store->records[ins].ip) - 1);
            g_store->records[ins].ip[sizeof(g_store->records[ins].ip) - 1] = '\0';
            g_store->occupied[ins] = SHM_SLOT_OCCUPIED;
            g_store->count++;
            return 0;
        }
        idx = (idx + 1) % g_store->capacity;
    } while (idx != start);

    if (first_tombstone != (size_t)-1) {
        memcpy(&g_store->records[first_tombstone], record, sizeof(brute_force_record_t));
        strncpy(g_store->records[first_tombstone].ip, ip,
                sizeof(g_store->records[first_tombstone].ip) - 1);
        g_store->records[first_tombstone].ip[sizeof(g_store->records[first_tombstone].ip) - 1] = '\0';
        g_store->occupied[first_tombstone] = SHM_SLOT_OCCUPIED;
        g_store->count++;
        return 0;
    }
    return -1;
}

static int builtin_shm_cleanup(time_t now)
{
    if (!g_store) return 0;
    int cleaned = 0;

    for (size_t i = 0; i < g_store->capacity; i++) {
        if (g_store->occupied[i] != SHM_SLOT_OCCUPIED) continue;
        brute_force_record_t *rec = &g_store->records[i];
        int expired = 0;
        if (rec->blocked_until > 0 && rec->blocked_until <= now)
            expired = 1;
        else if (rec->blocked_until == 0 && rec->first_attempt > 0 &&
                 (now - rec->first_attempt) > 600) /* 10 min staleness threshold */
            expired = 1;
        if (expired) {
            memset(rec, 0, sizeof(brute_force_record_t));
            g_store->occupied[i] = SHM_SLOT_TOMBSTONE;
            g_store->count--;
            cleaned++;
        }
    }
    return cleaned;
}

static void builtin_shm_destroy(void)
{
    if (!g_store) return;
    free(g_store->records);
    free(g_store->occupied);
    free(g_store);
    g_store = NULL;
}

/* ================================================================== */
/*  Atomic increment for brute force (avoids get+modify+put race)      */
/* ================================================================== */

/**
 * Atomically increment the attempt_count for an IP record.
 *
 * For native SHM mode, the entire read-modify-write is performed under
 * a single shmhash lock, preventing concurrent processes from losing
 * increments (the classic TOCTOU race between shm_get + shm_update).
 *
 * For the built-in fallback, the operation is inherently single-process
 * safe because OLS workers are single-threaded event loops.
 *
 * If no record exists for the IP, a new record is created with
 * attempt_count=1 and first_attempt=now.
 *
 * @param ip   Client IP address string.
 * @param now  Current time (used for new record creation).
 * @param out  If non-NULL, receives a copy of the record after increment.
 * @return 0 on success, -1 on failure.
 */
static int native_shm_atomic_increment(const char *ip, time_t now,
                                        brute_force_record_t *out)
{
    const ols_native_api_t *api = ols_native_api_get();
    if (!g_shm_ht || !api || !ip) return -1;

    api->shmhash_lock(g_shm_ht);

    ls_shmoff_t val_off = 0;
    ls_shmoff_t off = api->shmhash_get(g_shm_ht, ip, (int)strlen(ip), &val_off);
    if (off > 0 && val_off > 0) {
        brute_force_record_t *rec = (brute_force_record_t *)
            api->shmhash_off2ptr(g_shm_ht, val_off);
        if (rec) {
            rec->attempt_count++;
            if (out)
                memcpy(out, rec, sizeof(brute_force_record_t));
        }
    } else {
        /* Entry doesn't exist yet — shmhash_get may have created a zeroed
         * slot. Write initial record. */
        off = api->shmhash_get(g_shm_ht, ip, (int)strlen(ip), &val_off);
        if (off > 0 && val_off > 0) {
            brute_force_record_t *rec = (brute_force_record_t *)
                api->shmhash_off2ptr(g_shm_ht, val_off);
            if (rec) {
                memset(rec, 0, sizeof(brute_force_record_t));
                strncpy(rec->ip, ip, sizeof(rec->ip) - 1);
                rec->ip[sizeof(rec->ip) - 1] = '\0';
                rec->attempt_count = 1;
                rec->first_attempt = now;
                if (out)
                    memcpy(out, rec, sizeof(brute_force_record_t));
            }
        }
    }

    api->shmhash_unlock(g_shm_ht);
    return 0;
}

static int builtin_shm_atomic_increment(const char *ip, time_t now,
                                         brute_force_record_t *out)
{
    if (!g_store || !ip) return -1;

    brute_force_record_t *rec = builtin_shm_get(ip);
    if (rec) {
        rec->attempt_count++;
        if (out)
            memcpy(out, rec, sizeof(brute_force_record_t));
        return 0;
    }

    /* No record — create new one */
    brute_force_record_t new_rec;
    memset(&new_rec, 0, sizeof(new_rec));
    strncpy(new_rec.ip, ip, sizeof(new_rec.ip) - 1);
    new_rec.ip[sizeof(new_rec.ip) - 1] = '\0';
    new_rec.attempt_count = 1;
    new_rec.first_attempt = now;
    if (out)
        memcpy(out, &new_rec, sizeof(brute_force_record_t));
    return builtin_shm_update(ip, &new_rec);
}

/* ================================================================== */
/*  Public API — dispatches to native or built-in                      */
/* ================================================================== */

int shm_init(const char *shm_path, size_t max_records)
{
    (void)shm_path;

    if (max_records == 0) return -1;

    /* Try native ls_shmhash first */
    ols_native_api_init();
    if (ols_native_shm_available()) {
        if (native_shm_init(max_records) == 0) {
            g_use_native_shm = 1;
            return 0;
        }
    }
    /* Fallback to built-in */
    g_use_native_shm = 0;
    return builtin_shm_init(max_records);
}

brute_force_record_t *shm_get_record(const char *ip)
{
    if (g_use_native_shm)
        return native_shm_get(ip);
    return builtin_shm_get(ip);
}

int shm_update_record(const char *ip, const brute_force_record_t *record)
{
    if (g_use_native_shm)
        return native_shm_update(ip, record);
    return builtin_shm_update(ip, record);
}

int shm_atomic_increment(const char *ip, time_t now,
                         brute_force_record_t *out)
{
    if (g_use_native_shm)
        return native_shm_atomic_increment(ip, now, out);
    return builtin_shm_atomic_increment(ip, now, out);
}

int shm_cleanup_expired(time_t now)
{
    /* Native SHM doesn't need manual cleanup (OLS manages it) */
    if (g_use_native_shm)
        return 0;
    return builtin_shm_cleanup(now);
}

void shm_destroy(void)
{
    if (g_use_native_shm)
        native_shm_destroy();
    else
        builtin_shm_destroy();
    g_use_native_shm = 0;
}
