/**
 * ols_native_api.c - Runtime discovery of OLS native hash/SHM APIs
 *
 * Uses dlsym(RTLD_DEFAULT) to find ls_hash_* and ls_shmhash_* functions
 * exported by the OLS binary. If not found, the API table entries remain
 * NULL and callers fall back to their own implementations.
 */
#include "ols_native_api.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <string.h>

static ols_native_api_t g_native = {0};
static int g_initialized = 0;

int ols_native_api_init(void)
{
    if (g_initialized)
        return 0;

    g_initialized = 1;
    memset(&g_native, 0, sizeof(g_native));

    /* ls_hash functions */
    g_native.hash_new      = (ls_hash_new_fn)dlsym(RTLD_DEFAULT, "ls_hash_new");
    g_native.hash_find     = (ls_hash_find_fn)dlsym(RTLD_DEFAULT, "ls_hash_find");
    g_native.hash_insert   = (ls_hash_insert_fn)dlsym(RTLD_DEFAULT, "ls_hash_insert");
    g_native.hash_getdata  = (ls_hash_getdata_fn)dlsym(RTLD_DEFAULT, "ls_hash_getdata");
    g_native.hash_delete   = (ls_hash_delete_fn)dlsym(RTLD_DEFAULT, "ls_hash_delete");
    g_native.hash_hfstring = (ls_hash_hfstring_fn)dlsym(RTLD_DEFAULT, "ls_hash_hfstring");
    g_native.hash_cmpstring = (ls_hash_cmpstring_fn)dlsym(RTLD_DEFAULT, "ls_hash_cmpstring");

    /* ls_shmhash functions */
    g_native.shm_opengpool  = (ls_shm_opengpool_fn)dlsym(RTLD_DEFAULT, "ls_shm_opengpool");
    g_native.shmhash_open   = (ls_shmhash_open_fn)dlsym(RTLD_DEFAULT, "ls_shmhash_open");
    g_native.shmhash_get    = (ls_shmhash_get_fn)dlsym(RTLD_DEFAULT, "ls_shmhash_get");
    g_native.shmhash_off2ptr = (ls_shmhash_off2ptr_fn)dlsym(RTLD_DEFAULT, "ls_shmhash_off2ptr");
    g_native.shmhash_lock   = (ls_shmhash_lock_fn)dlsym(RTLD_DEFAULT, "ls_shmhash_lock");
    g_native.shmhash_unlock = (ls_shmhash_unlock_fn)dlsym(RTLD_DEFAULT, "ls_shmhash_unlock");

    return 0;
}

int ols_native_hash_available(void)
{
    return g_native.hash_new && g_native.hash_find &&
           g_native.hash_insert && g_native.hash_getdata &&
           g_native.hash_hfstring && g_native.hash_cmpstring;
}

int ols_native_shm_available(void)
{
    return g_native.shm_opengpool && g_native.shmhash_open &&
           g_native.shmhash_get && g_native.shmhash_off2ptr &&
           g_native.shmhash_lock && g_native.shmhash_unlock;
}

const ols_native_api_t *ols_native_api_get(void)
{
    return g_initialized ? &g_native : NULL;
}
