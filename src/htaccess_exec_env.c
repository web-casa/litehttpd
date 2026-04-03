/**
 * htaccess_exec_env.c - Environment variable directive executor
 *
 * Validates: Requirements 11.1, 11.2, 11.3, 11.4, 11.5, 11.6
 */
#include "htaccess_exec_env.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <regex.h>

/* ------------------------------------------------------------------ */
/*  Thread-local regex cache for SetEnvIf patterns                     */
/* ------------------------------------------------------------------ */

#define ENV_REGEX_CACHE_SLOTS 8

static __thread struct {
    unsigned long hash;   /* djb2 hash of pattern + flags */
    char         *pat;    /* owned copy of pattern string */
    regex_t       re;
    int           valid;
} env_regex_cache[ENV_REGEX_CACHE_SLOTS];

static unsigned long env_djb2(const char *str, int flags)
{
    unsigned long h = 5381 + (unsigned)flags;
    while (*str)
        h = ((h << 5) + h) + (unsigned char)*str++;
    return h;
}

static regex_t *env_get_cached_regex(const char *pattern, int flags)
{
    unsigned long h = env_djb2(pattern, flags);
    for (int i = 0; i < ENV_REGEX_CACHE_SLOTS; i++) {
        if (env_regex_cache[i].valid && env_regex_cache[i].hash == h
            && strcmp(env_regex_cache[i].pat, pattern) == 0)
            return &env_regex_cache[i].re;
    }
    int slot = -1;
    for (int i = 0; i < ENV_REGEX_CACHE_SLOTS; i++) {
        if (!env_regex_cache[i].valid) { slot = i; break; }
    }
    if (slot < 0) {
        regfree(&env_regex_cache[0].re);
        free(env_regex_cache[0].pat);
        for (int i = 0; i < ENV_REGEX_CACHE_SLOTS - 1; i++)
            env_regex_cache[i] = env_regex_cache[i + 1];
        slot = ENV_REGEX_CACHE_SLOTS - 1;
        env_regex_cache[slot].valid = 0;
    }
    if (regcomp(&env_regex_cache[slot].re, pattern, flags) != 0)
        return NULL;
    env_regex_cache[slot].hash = h;
    env_regex_cache[slot].pat = strdup(pattern);
    if (!env_regex_cache[slot].pat) {
        regfree(&env_regex_cache[slot].re);
        return NULL;
    }
    env_regex_cache[slot].valid = 1;
    return &env_regex_cache[slot].re;
}

/**
 * Check if an environment variable name is blacklisted.
 * Blocks LD_* (dynamic linker hijack), GLIBC_* (glibc tunables),
 * and other dangerous variables. Matches Apache behavior.
 *
 * Returns 1 if blocked, 0 if allowed.
 */
int is_env_blacklisted(const char *name)
{
    if (!name) return 0;

    /* LD_* — dynamic linker hijack (LD_PRELOAD, LD_LIBRARY_PATH, etc.) */
    if (strncasecmp(name, "LD_", 3) == 0) return 1;

    /* GLIBC_TUNABLES — glibc tuning attacks */
    if (strncasecmp(name, "GLIBC_", 6) == 0) return 1;

    /* MALLOC_* — memory allocator attacks */
    if (strncasecmp(name, "MALLOC_", 7) == 0) return 1;

    /* Shell injection vectors */
    if (strcasecmp(name, "IFS") == 0) return 1;
    if (strcasecmp(name, "PATH") == 0) return 1;
    if (strcasecmp(name, "BASH_ENV") == 0) return 1;
    if (strcasecmp(name, "ENV") == 0) return 1;
    if (strcasecmp(name, "CDPATH") == 0) return 1;

    /* Proxy hijack */
    if (strcasecmp(name, "HTTP_PROXY") == 0) return 1;

    return 0;
}

static const char *get_attribute_value(lsi_session_t *session,
                                       const char *attribute,
                                       int *out_len)
{
    if (!attribute || !out_len)
        return NULL;
    if (strcmp(attribute, "Remote_Addr") == 0)
        return lsi_session_get_client_ip(session, out_len);
    if (strcmp(attribute, "Request_URI") == 0)
        return lsi_session_get_uri(session, out_len);
    if (strcmp(attribute, "Query_String") == 0)
        return lsi_session_get_query_string(session, out_len);
    if (strcmp(attribute, "Request_Method") == 0)
        return lsi_session_get_method(session, out_len);
    int attr_len = (int)strlen(attribute);
    return lsi_session_get_req_header_by_name(
        session, attribute, attr_len, out_len);
}

static int match_and_set(lsi_session_t *session,
                         const htaccess_directive_t *dir,
                         const char *attr_value,
                         int nocase)
{
    int rc;
    int flags = REG_EXTENDED | REG_NOSUB;
    if (nocase)
        flags |= REG_ICASE;
    if (!dir->data.envif.pattern)
        return LSI_ERROR;
    if (strlen(dir->data.envif.pattern) > 512)
        return LSI_ERROR; /* ReDoS prevention */
    regex_t *rep = env_get_cached_regex(dir->data.envif.pattern, flags);
    if (!rep)
        return LSI_ERROR;
    rc = regexec(rep, attr_value, 0, NULL, 0);
    if (rc != 0)
        return LSI_OK;
    if (!dir->name)
        return LSI_ERROR;
    if (is_env_blacklisted(dir->name)) {
        lsi_log(session, LSI_LOG_WARN,
                "[htaccess] blocked dangerous env var in SetEnvIf: %s", dir->name);
        return LSI_OK;
    }
    int name_len = (int)strlen(dir->name);
    int val_len = dir->value ? (int)strlen(dir->value) : 0;
    const char *val = dir->value ? dir->value : "";
    return lsi_session_set_env(session, dir->name,
                               name_len, val, val_len);
}

int exec_setenv(lsi_session_t *session, const htaccess_directive_t *dir)
{
    if (!session || !dir || !dir->name)
        return LSI_ERROR;
    if (dir->type != DIR_SETENV)
        return LSI_ERROR;
    if (is_env_blacklisted(dir->name)) {
        lsi_log(session, LSI_LOG_WARN,
                "[htaccess] blocked dangerous env var: %s", dir->name);
        return LSI_OK; /* silently skip, don't fail request */
    }
    int name_len = (int)strlen(dir->name);
    int val_len = dir->value ? (int)strlen(dir->value) : 0;
    const char *val = dir->value ? dir->value : "";
    return lsi_session_set_env(session, dir->name,
                               name_len, val, val_len);
}

int exec_setenvif(lsi_session_t *session, const htaccess_directive_t *dir)
{
    int attr_len = 0;
    const char *attr_value;
    int nocase;
    if (!session || !dir)
        return LSI_ERROR;
    if (dir->type != DIR_SETENVIF && dir->type != DIR_SETENVIF_NOCASE)
        return LSI_ERROR;
    if (!dir->data.envif.attribute)
        return LSI_ERROR;
    attr_value = get_attribute_value(session, dir->data.envif.attribute,
                                     &attr_len);
    /* Allow empty string to match patterns like ^$ */
    if (!attr_value) {
        attr_value = "";
        attr_len = 0;
    }
    nocase = (dir->type == DIR_SETENVIF_NOCASE) ? 1 : 0;
    return match_and_set(session, dir, attr_value, nocase);
}

int exec_browser_match(lsi_session_t *session, const htaccess_directive_t *dir)
{
    int ua_len = 0;
    const char *ua;
    if (!session || !dir)
        return LSI_ERROR;
    if (dir->type != DIR_BROWSER_MATCH)
        return LSI_ERROR;
    ua = lsi_session_get_req_header_by_name(session,
        "User-Agent", 10, &ua_len);
    if (!ua || ua_len <= 0)
        return LSI_OK;
    return match_and_set(session, dir, ua, 0);
}