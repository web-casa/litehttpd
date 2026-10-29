/**
 * htaccess_exec_env.c - Environment variable directive executor
 *
 * Validates: Requirements 11.1, 11.2, 11.3, 11.4, 11.5, 11.6
 */
#include "htaccess_exec_env.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "htaccess_regex.h"

/* ------------------------------------------------------------------ */
/*  Thread-local regex cache for SetEnvIf patterns                     */
/* ------------------------------------------------------------------ */

/* Regex via unified wrapper; HTRE flags: 0x1 = caseless. */
#define ENV_REGEX_TAG 1

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
    if (!dir->data.envif.pattern)
        return LSI_ERROR;
    htre_regex_t *rep = htre_get_cached(ENV_REGEX_TAG, dir->data.envif.pattern,
                                        nocase ? 0x1 : 0, NULL, 0);
    if (!rep)
        return LSI_ERROR;
    rc = htre_exec(rep, attr_value, 0, NULL);
    if (rc == HTRE_HIT_LIMIT || rc == HTRE_ERROR) {
        /* Enable-class rule: warn and skip (spec regex-engine.md §3). */
        lsi_log(session, LSI_LOG_WARN,
                "[htaccess] SetEnvIf: regex resource limit hit for %s, rule skipped",
                dir->data.envif.pattern);
        return LSI_OK;
    }
    if (rc != HTRE_MATCH)
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