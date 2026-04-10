/**
 * ols_native_api.h - OLS native ls_hash / ls_shmhash API declarations
 *
 * These functions are exported by the OLS binary but NOT part of the
 * standard lsi_api_t. We discover them at runtime via dlsym().
 *
 * API signatures derived from CyberPanel's custom OLS binary
 * (openlitespeed-phpconfig-x86_64-ubuntu-static).
 *
 * Usage:
 *   ols_native_api_init();  // call once at module init
 *   if (ols_native_hash_available()) { ... use ls_hash ... }
 *   if (ols_native_shm_available()) { ... use ls_shmhash ... }
 */
#ifndef OLS_NATIVE_API_H
#define OLS_NATIVE_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  ls_hash — in-process hash table (for .htaccess cache)              */
/* ------------------------------------------------------------------ */

typedef void *ls_hash_t;           /* opaque hash table handle */
typedef void *ls_hashentry_t;      /* opaque hash entry handle */

/* Hash/compare function types */
typedef unsigned long (*ls_hash_fn)(const void *key);
typedef int (*ls_hash_cmp_fn)(const void *key1, const void *key2);

/* Function pointer types for dlsym */
typedef ls_hash_t (*ls_hash_new_fn)(int size, ls_hash_fn hf, ls_hash_cmp_fn cf);
typedef ls_hashentry_t (*ls_hash_find_fn)(ls_hash_t ht, const void *key);
typedef ls_hashentry_t (*ls_hash_insert_fn)(ls_hash_t ht, const void *key, void *data);
typedef void *(*ls_hash_getdata_fn)(ls_hashentry_t entry);
typedef void (*ls_hash_delete_fn)(ls_hash_t ht);

/* Built-in hash/compare for strings */
typedef unsigned long (*ls_hash_hfstring_fn)(const void *key);
typedef int (*ls_hash_cmpstring_fn)(const void *key1, const void *key2);

/* ------------------------------------------------------------------ */
/*  ls_shmhash — shared memory hash table (for brute force IP track)   */
/* ------------------------------------------------------------------ */

typedef void *ls_shmpool_t;        /* opaque SHM pool handle */
typedef void *ls_shmhash_t;        /* opaque SHM hash table handle */
typedef long  ls_shmoff_t;         /* SHM offset type */

/* Function pointer types for dlsym */
typedef ls_shmpool_t (*ls_shm_opengpool_fn)(const char *name, int size);
typedef ls_shmhash_t (*ls_shmhash_open_fn)(ls_shmpool_t pool, const char *name,
                                            int init_size, int hash_size);
typedef ls_shmoff_t (*ls_shmhash_get_fn)(ls_shmhash_t ht, const void *key,
                                          int key_len, ls_shmoff_t *val_off);
typedef void *(*ls_shmhash_off2ptr_fn)(ls_shmhash_t ht, ls_shmoff_t offset);
typedef int (*ls_shmhash_lock_fn)(ls_shmhash_t ht);
typedef int (*ls_shmhash_unlock_fn)(ls_shmhash_t ht);

/* ------------------------------------------------------------------ */
/*  Runtime API table                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    /* ls_hash functions */
    ls_hash_new_fn          hash_new;
    ls_hash_find_fn         hash_find;
    ls_hash_insert_fn       hash_insert;
    ls_hash_getdata_fn      hash_getdata;
    ls_hash_delete_fn       hash_delete;
    ls_hash_hfstring_fn     hash_hfstring;
    ls_hash_cmpstring_fn    hash_cmpstring;

    /* ls_shmhash functions */
    ls_shm_opengpool_fn     shm_opengpool;
    ls_shmhash_open_fn      shmhash_open;
    ls_shmhash_get_fn       shmhash_get;
    ls_shmhash_off2ptr_fn   shmhash_off2ptr;
    ls_shmhash_lock_fn      shmhash_lock;
    ls_shmhash_unlock_fn    shmhash_unlock;
} ols_native_api_t;

/**
 * Initialize the native API table by looking up symbols via dlsym.
 * Safe to call multiple times (idempotent).
 * @return 0 on success (at least some symbols found), -1 on failure.
 */
int ols_native_api_init(void);

/**
 * @return 1 if ls_hash functions are available, 0 otherwise.
 */
int ols_native_hash_available(void);

/**
 * @return 1 if ls_shmhash functions are available, 0 otherwise.
 */
int ols_native_shm_available(void);

/**
 * Get the native API table. Returns NULL if init not called.
 */
const ols_native_api_t *ols_native_api_get(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OLS_NATIVE_API_H */
