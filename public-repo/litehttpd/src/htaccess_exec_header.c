/**
 * htaccess_exec_header.c - Header and RequestHeader directive executors
 *
 * Implements execution of Header (set/unset/append/merge/add/edit/edit*) and
 * RequestHeader (set/unset) directives via LSIAPI session calls.
 * Supports env=VAR conditional execution and Header edit regex replacement.
 *
 * Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7
 */
#include "htaccess_exec_header.h"
#include <string.h>
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>

/* Thread-local regex cache for Header edit patterns */
#define HDR_REGEX_CACHE_SLOTS 4

/* Thread-local shadow table: tracks headers set during this request
 * so that sequential append/merge can read back our own values,
 * working around OLS API limitation where get_resp_header doesn't
 * reflect headers set via set_resp_header in the same hook. */
#define HDR_SHADOW_SLOTS 16
static __thread struct {
    char name[128];
    char value[4096];
    int  name_len;
    int  val_len;
} hdr_shadow[HDR_SHADOW_SLOTS];
static __thread int hdr_shadow_count;

static const char *shadow_get(const char *name, int name_len, int *out_len)
{
    for (int i = 0; i < hdr_shadow_count; i++) {
        if (hdr_shadow[i].name_len == name_len &&
            strncasecmp(hdr_shadow[i].name, name, (size_t)name_len) == 0) {
            if (out_len) *out_len = hdr_shadow[i].val_len;
            return hdr_shadow[i].value;
        }
    }
    return NULL;
}

static void shadow_set(const char *name, int name_len,
                       const char *value, int val_len)
{
    /* Update existing entry */
    for (int i = 0; i < hdr_shadow_count; i++) {
        if (hdr_shadow[i].name_len == name_len &&
            strncasecmp(hdr_shadow[i].name, name, (size_t)name_len) == 0) {
            if (val_len >= (int)sizeof(hdr_shadow[i].value))
                val_len = (int)sizeof(hdr_shadow[i].value) - 1;
            memcpy(hdr_shadow[i].value, value, (size_t)val_len);
            hdr_shadow[i].value[val_len] = '\0';
            hdr_shadow[i].val_len = val_len;
            return;
        }
    }
    /* Add new entry — evict oldest if full */
    if (hdr_shadow_count >= HDR_SHADOW_SLOTS) {
        /* Evict slot 0 (FIFO) to make room */
        for (int k = 0; k < HDR_SHADOW_SLOTS - 1; k++)
            hdr_shadow[k] = hdr_shadow[k + 1];
        hdr_shadow_count = HDR_SHADOW_SLOTS - 1;
    }
    {
        int i = hdr_shadow_count++;
        if (name_len >= (int)sizeof(hdr_shadow[i].name))
            name_len = (int)sizeof(hdr_shadow[i].name) - 1;
        memcpy(hdr_shadow[i].name, name, (size_t)name_len);
        hdr_shadow[i].name[name_len] = '\0';
        hdr_shadow[i].name_len = name_len;
        if (val_len >= (int)sizeof(hdr_shadow[i].value))
            val_len = (int)sizeof(hdr_shadow[i].value) - 1;
        memcpy(hdr_shadow[i].value, value, (size_t)val_len);
        hdr_shadow[i].value[val_len] = '\0';
        hdr_shadow[i].val_len = val_len;
    }
}

static void shadow_remove(const char *name, int name_len)
{
    for (int i = 0; i < hdr_shadow_count; i++) {
        if (hdr_shadow[i].name_len == name_len &&
            strncasecmp(hdr_shadow[i].name, name, (size_t)name_len) == 0) {
            hdr_shadow[i] = hdr_shadow[--hdr_shadow_count];
            return;
        }
    }
}

void exec_header_reset_shadow(void)
{
    hdr_shadow_count = 0;
}

static __thread struct {
    unsigned long hash;
    char         *pat;    /* owned copy of pattern string */
    regex_t     re;
    int         valid;
} hdr_regex_cache[HDR_REGEX_CACHE_SLOTS];

static unsigned long hdr_djb2(const char *str)
{
    unsigned long h = 5381;
    while (*str)
        h = ((h << 5) + h) + (unsigned char)*str++;
    return h;
}

static regex_t *hdr_get_cached_regex(const char *pattern)
{
    unsigned long h = hdr_djb2(pattern);
    for (int i = 0; i < HDR_REGEX_CACHE_SLOTS; i++) {
        if (hdr_regex_cache[i].valid && hdr_regex_cache[i].hash == h
            && strcmp(hdr_regex_cache[i].pat, pattern) == 0)
            return &hdr_regex_cache[i].re;
    }
    int slot = -1;
    for (int i = 0; i < HDR_REGEX_CACHE_SLOTS; i++) {
        if (!hdr_regex_cache[i].valid) { slot = i; break; }
    }
    if (slot < 0) {
        regfree(&hdr_regex_cache[0].re);
        free(hdr_regex_cache[0].pat);
        for (int i = 0; i < HDR_REGEX_CACHE_SLOTS - 1; i++)
            hdr_regex_cache[i] = hdr_regex_cache[i + 1];
        slot = HDR_REGEX_CACHE_SLOTS - 1;
        hdr_regex_cache[slot].valid = 0;
    }
    if (regcomp(&hdr_regex_cache[slot].re, pattern, REG_EXTENDED) != 0)
        return NULL;
    hdr_regex_cache[slot].hash = h;
    hdr_regex_cache[slot].pat = strdup(pattern);
    if (!hdr_regex_cache[slot].pat) {
        regfree(&hdr_regex_cache[slot].re);
        return NULL;
    }
    hdr_regex_cache[slot].valid = 1;
    return &hdr_regex_cache[slot].re;
}

/**
 * Expand Apache-style %{VAR}e environment variable references in a value.
 * Returns a static thread-local buffer with expanded content.
 * If no %{...}e patterns found, returns the original pointer unchanged.
 */
static const char *expand_env_vars(lsi_session_t *session,
                                   const char *value, int *out_len)
{
    if (!value || !strchr(value, '%'))
        return value;

    static __thread char expanded[4096];
    size_t pos = 0;
    const char *p = value;

    while (*p && pos < sizeof(expanded) - 1) {
        if (p[0] == '%' && p[1] == '{') {
            /* Find closing }e */
            const char *close = strchr(p + 2, '}');
            if (close && close[1] == 'e') {
                /* Extract variable name */
                size_t name_len = (size_t)(close - p - 2);
                char var_name[256];
                if (name_len < sizeof(var_name)) {
                    memcpy(var_name, p + 2, name_len);
                    var_name[name_len] = '\0';

                    int env_len = 0;
                    const char *env_val = lsi_session_get_env(
                        session, var_name, (int)name_len, &env_len);
                    if (env_val && env_len > 0) {
                        if (pos + (size_t)env_len < sizeof(expanded) - 1) {
                            memcpy(expanded + pos, env_val, (size_t)env_len);
                            pos += (size_t)env_len;
                        }
                    }
                }
                p = close + 2; /* skip }e */
                continue;
            }
        }
        expanded[pos++] = *p++;
    }
    expanded[pos] = '\0';
    if (out_len) *out_len = (int)pos;
    return expanded;
}

/**
 * Check if a substring exists within a comma-separated header value.
 * Used by merge to ensure idempotency.
 *
 * @param haystack  The current header value (comma-separated).
 * @param hay_len   Length of haystack.
 * @param needle    The value to search for.
 * @param needle_len Length of needle.
 * @return 1 if needle is found as a token, 0 otherwise.
 */
static int value_exists_in_header(const char *haystack, int hay_len,
                                  const char *needle, int needle_len)
{
    if (!haystack || hay_len <= 0 || !needle || needle_len <= 0)
        return 0;

    const char *p = haystack;
    const char *end = haystack + hay_len;

    while (p < end) {
        /* Skip leading whitespace */
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;

        /* Find end of current token (next comma or end) */
        const char *tok_start = p;
        while (p < end && *p != ',')
            p++;

        /* Trim trailing whitespace from token */
        const char *tok_end = p;
        while (tok_end > tok_start && (*(tok_end - 1) == ' ' || *(tok_end - 1) == '\t'))
            tok_end--;

        int tok_len = (int)(tok_end - tok_start);
        if (tok_len == needle_len && memcmp(tok_start, needle, (size_t)needle_len) == 0)
            return 1;

        /* Skip comma */
        if (p < end)
            p++;
    }
    return 0;
}

/**
 * Check if the env=VAR condition on a header directive is satisfied.
 * Returns 1 if the directive should execute, 0 if it should be skipped.
 *
 * Supports negation: env=!VAR means "execute if VAR is NOT set".
 */
static int check_env_condition(lsi_session_t *session,
                               const htaccess_directive_t *dir)
{
    if (!dir->env_condition)
        return 1; /* No condition — always execute */

    const char *var_name = dir->env_condition;
    int negated = 0;

    if (var_name[0] == '!') {
        negated = 1;
        var_name++;
    }

    if (var_name[0] == '\0')
        return 1; /* Empty var name — always execute */

    int val_len = 0;
    const char *val = lsi_session_get_env(session, var_name,
                                           (int)strlen(var_name), &val_len);

    int is_set = (val != NULL);
    return negated ? !is_set : is_set;
}

/**
 * Perform regex-based header value editing.
 * For edit: replaces the first match.
 * For edit*: replaces all matches (global).
 *
 * @param session    LSIAPI session handle.
 * @param dir        Directive with edit_pattern (regex) and value (replacement).
 * @param global     1 for edit* (replace all), 0 for edit (first only).
 * @return LSI_OK on success, LSI_ERROR on failure.
 */
static int exec_header_edit(lsi_session_t *session,
                            const htaccess_directive_t *dir,
                            int global)
{
    if (!dir->name || !dir->data.header_ext.edit_pattern || !dir->value)
        return LSI_ERROR;

    int name_len = (int)strlen(dir->name);

    /* Get current header value */
    int cur_len = 0;
    const char *cur_val = lsi_session_get_resp_header_by_name(
        session, dir->name, name_len, &cur_len);

    if (!cur_val || cur_len <= 0)
        return LSI_OK; /* Header not present — nothing to edit */

    /* Compile regex (cached) */
    if (strlen(dir->data.header_ext.edit_pattern) > 512)
        return LSI_ERROR; /* ReDoS prevention */
    regex_t *rep = hdr_get_cached_regex(dir->data.header_ext.edit_pattern);
    if (!rep)
        return LSI_ERROR;

    /* Build result by replacing matches with dir->value (replacement).
     * Support $1..$9 backreferences in replacement text.
     * Expand %{VAR}e and reject CR/LF in replacement (header injection defense). */
    const char *replacement = dir->value;
    int repl_len = replacement ? (int)strlen(replacement) : 0;
    if (replacement) {
        replacement = expand_env_vars(session, replacement, &repl_len);
        if (memchr(replacement, '\r', repl_len) || memchr(replacement, '\n', repl_len))
            return LSI_ERROR;
    }

    int overflow = 0;
    char result[4096];
    size_t result_len = 0;
    const char *src = cur_val;
    int src_len = cur_len;
    regmatch_t matches[10]; /* $0..$9 */

    while (src_len > 0 && regexec(rep, src, 10, matches, 0) == 0) {
        if (matches[0].rm_so < 0 || matches[0].rm_eo < 0)
            break;

        /* Copy text before match */
        size_t before_len = (size_t)matches[0].rm_so;
        if (result_len + before_len >= sizeof(result) - 1) {
            overflow = 1;
            break;
        }
        memcpy(result + result_len, src, before_len);
        result_len += before_len;

        /* Copy replacement with $N backreference expansion */
        const char *rp = replacement ? replacement : "";
        while (*rp && result_len < sizeof(result) - 1) {
            if (*rp == '$' && rp[1] >= '0' && rp[1] <= '9') {
                int idx = rp[1] - '0';
                rp += 2;
                if (idx < 10 && matches[idx].rm_so >= 0 &&
                    matches[idx].rm_eo > matches[idx].rm_so) {
                    size_t cap_len = (size_t)(matches[idx].rm_eo - matches[idx].rm_so);
                    if (result_len + cap_len < sizeof(result) - 1) {
                        memcpy(result + result_len, src + matches[idx].rm_so, cap_len);
                        result_len += cap_len;
                    } else {
                        overflow = 1;
                    }
                }
            } else {
                result[result_len++] = *rp++;
            }
        }
        if (*rp) overflow = 1; /* replacement not fully consumed */

        /* Advance past match */
        src += matches[0].rm_eo;
        src_len -= matches[0].rm_eo;

        /* Prevent infinite loop on zero-length match */
        if (matches[0].rm_so == matches[0].rm_eo) {
            if (src_len > 0) {
                if (result_len < sizeof(result) - 1)
                    result[result_len++] = *src;
                src++;
                src_len--;
            }
        }

        if (!global)
            break; /* edit: first match only */
    }

    /* Copy remaining text */
    if (src_len > 0 && result_len + (size_t)src_len < sizeof(result) - 1) {
        memcpy(result + result_len, src, (size_t)src_len);
        result_len += (size_t)src_len;
    } else if (src_len > 0) {
        overflow = 1;
    }
    result[result_len] = '\0';

    if (overflow)
        return LSI_ERROR;

    /* Set the modified header value */
    return lsi_session_set_resp_header(session, dir->name, name_len,
                                       result, (int)result_len);
}

int exec_header(lsi_session_t *session, const htaccess_directive_t *dir)
{
    if (!session || !dir || !dir->name)
        return LSI_ERROR;

    /* Reject header names containing CR/LF (header injection defense) */
    if (strchr(dir->name, '\r') || strchr(dir->name, '\n'))
        return LSI_ERROR;

    /* Check env=VAR condition */
    if (!check_env_condition(session, dir))
        return LSI_OK; /* Condition not met — skip silently */

    int name_len = (int)strlen(dir->name);

    /* Expand %{VAR}e references in the value */
    int val_len = dir->value ? (int)strlen(dir->value) : 0;
    const char *value = dir->value;
    if (value) {
        value = expand_env_vars(session, value, &val_len);
        /* Reject values containing CR/LF (header injection defense) */
        if (memchr(value, '\r', val_len) || memchr(value, '\n', val_len))
            return LSI_ERROR;
    }

    switch (dir->type) {
    case DIR_HEADER_SET:
    case DIR_HEADER_ALWAYS_SET: {
        if (!value)
            return LSI_ERROR;
        int rc = lsi_session_set_resp_header(session, dir->name, name_len,
                                              value, val_len);
        if (rc == LSI_OK)
            shadow_set(dir->name, name_len, value, val_len);
        return rc;
    }

    case DIR_HEADER_UNSET:
    case DIR_HEADER_ALWAYS_UNSET: {
        int rc = lsi_session_remove_resp_header(session, dir->name, name_len);
        if (rc == LSI_OK)
            shadow_remove(dir->name, name_len);
        return rc;
    }

    case DIR_HEADER_APPEND:
    case DIR_HEADER_ALWAYS_APPEND: {
        if (!value)
            return LSI_ERROR;
        int cur_len = 0;
        const char *cur = shadow_get(dir->name, name_len, &cur_len);
        if (!cur || cur_len <= 0)
            cur = lsi_session_get_resp_header_by_name(
                session, dir->name, name_len, &cur_len);
        if (cur && cur_len > 0) {
            char buf[4096];
            int blen = snprintf(buf, sizeof(buf), "%.*s, %.*s",
                                cur_len, cur, val_len, value);
            if (blen > 0 && blen < (int)sizeof(buf)) {
                lsi_session_remove_resp_header(session, dir->name, name_len);
                int rc = lsi_session_set_resp_header(session, dir->name, name_len,
                                                      buf, blen);
                if (rc == LSI_OK)
                    shadow_set(dir->name, name_len, buf, blen);
                return rc;
            }
            return LSI_ERROR; /* overflow */
        }
        /* No existing value — just set */
        int rc = lsi_session_set_resp_header(session, dir->name, name_len,
                                              value, val_len);
        if (rc == LSI_OK)
            shadow_set(dir->name, name_len, value, val_len);
        return rc;
    }

    case DIR_HEADER_MERGE:
    case DIR_HEADER_ALWAYS_MERGE: {
        if (!value)
            return LSI_ERROR;

        int cur_len = 0;
        const char *cur_val = shadow_get(dir->name, name_len, &cur_len);
        if (!cur_val || cur_len <= 0)
            cur_val = lsi_session_get_resp_header_by_name(
                session, dir->name, name_len, &cur_len);

        if (cur_val && cur_len > 0) {
            if (value_exists_in_header(cur_val, cur_len, value, val_len))
                return LSI_OK; /* Already present, skip */
            char buf[4096];
            int blen = snprintf(buf, sizeof(buf), "%.*s, %.*s",
                                cur_len, cur_val, val_len, value);
            if (blen > 0 && blen < (int)sizeof(buf)) {
                lsi_session_remove_resp_header(session, dir->name, name_len);
                int rc = lsi_session_set_resp_header(session, dir->name, name_len,
                                                      buf, blen);
                if (rc == LSI_OK)
                    shadow_set(dir->name, name_len, buf, blen);
                return rc;
            }
            return LSI_ERROR;
        }

        /* No existing value — just set */
        int rc = lsi_session_set_resp_header(session, dir->name, name_len,
                                              value, val_len);
        if (rc == LSI_OK)
            shadow_set(dir->name, name_len, value, val_len);
        return rc;
    }

    case DIR_HEADER_ADD:
    case DIR_HEADER_ALWAYS_ADD: {
        if (!value)
            return LSI_ERROR;
        int rc = lsi_session_add_resp_header(session, dir->name, name_len,
                                              value, val_len);
        if (rc == LSI_OK) {
            /* Update shadow: track cumulative value for subsequent append/merge */
            int cur_len = 0;
            const char *cur = shadow_get(dir->name, name_len, &cur_len);
            if (cur && cur_len > 0) {
                char buf[4096];
                int blen = snprintf(buf, sizeof(buf), "%.*s, %.*s",
                                    cur_len, cur, val_len, value);
                if (blen > 0 && blen < (int)sizeof(buf))
                    shadow_set(dir->name, name_len, buf, blen);
            } else {
                shadow_set(dir->name, name_len, value, val_len);
            }
        }
        return rc;
    }

    case DIR_HEADER_EDIT:
    case DIR_HEADER_ALWAYS_EDIT:
        return exec_header_edit(session, dir, 0);

    case DIR_HEADER_EDIT_STAR:
    case DIR_HEADER_ALWAYS_EDIT_STAR:
        return exec_header_edit(session, dir, 1);

    default:
        return LSI_ERROR;
    }
}

int exec_request_header(lsi_session_t *session, const htaccess_directive_t *dir)
{
    if (!session || !dir || !dir->name)
        return LSI_ERROR;

    /* Reject CRLF injection in header name */
    if (strchr(dir->name, '\r') || strchr(dir->name, '\n'))
        return LSI_ERROR;

    int name_len = (int)strlen(dir->name);

    switch (dir->type) {
    case DIR_REQUEST_HEADER_SET: {
        if (!dir->value)
            return LSI_ERROR;
        /* Reject CRLF injection in header value */
        if (strchr(dir->value, '\r') || strchr(dir->value, '\n'))
            return LSI_ERROR;
        int val_len = (int)strlen(dir->value);
        return lsi_session_set_req_header(session, dir->name, name_len,
                                          dir->value, val_len);
    }

    case DIR_REQUEST_HEADER_UNSET:
        return lsi_session_remove_req_header(session, dir->name, name_len);

    default:
        return LSI_ERROR;
    }
}
