/**
 * htaccess_exec_redirect.c - Redirect and RedirectMatch directive executors
 *
 * Implements Redirect (prefix match) and RedirectMatch (regex + $N
 * backreference substitution). Both functions return 1 on redirect
 * (short-circuit signal), 0 on no match, -1 on error.
 *
 * Validates: Requirements 7.1, 7.2, 7.3, 7.4, 7.5
 */
#include "htaccess_exec_redirect.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <regex.h>

/** Maximum number of regex capture groups supported. */
#define MAX_CAPTURES 10

/** Maximum length of the substituted Location URL. */
#define MAX_URL_LEN 4096

/** Maximum regex pattern length to prevent ReDoS. */
#define MAX_REGEX_LEN 512

/* ------------------------------------------------------------------ */
/*  Thread-local regex cache — avoids regcomp on every request          */
/* ------------------------------------------------------------------ */

#define REGEX_CACHE_SLOTS 8

static __thread struct {
    unsigned long hash;    /* djb2 hash of pattern + flags */
    char         *pat;     /* owned copy of pattern string */
    regex_t       re;      /* compiled regex */
    int           valid;
} regex_cache[REGEX_CACHE_SLOTS];

static unsigned long redir_djb2(const char *str, int flags)
{
    unsigned long h = 5381 + (unsigned)flags;
    while (*str)
        h = ((h << 5) + h) + (unsigned char)*str++;
    return h;
}

static regex_t *get_cached_regex(const char *pattern, int flags)
{
    unsigned long h = redir_djb2(pattern, flags);
    for (int i = 0; i < REGEX_CACHE_SLOTS; i++) {
        if (regex_cache[i].valid && regex_cache[i].hash == h
            && strcmp(regex_cache[i].pat, pattern) == 0)
            return &regex_cache[i].re;
    }
    int slot = -1;
    for (int i = 0; i < REGEX_CACHE_SLOTS; i++) {
        if (!regex_cache[i].valid) { slot = i; break; }
    }
    if (slot < 0) {
        regfree(&regex_cache[0].re);
        free(regex_cache[0].pat);
        for (int i = 0; i < REGEX_CACHE_SLOTS - 1; i++)
            regex_cache[i] = regex_cache[i + 1];
        slot = REGEX_CACHE_SLOTS - 1;
        regex_cache[slot].valid = 0;
    }
    if (regcomp(&regex_cache[slot].re, pattern, flags) != 0)
        return NULL;
    regex_cache[slot].hash = h;
    regex_cache[slot].pat = strdup(pattern);
    if (!regex_cache[slot].pat) {
        regfree(&regex_cache[slot].re);
        return NULL;
    }
    regex_cache[slot].valid = 1;
    return &regex_cache[slot].re;
}

/**
 * Substitute $N backreferences in a template string with captured values.
 *
 * @param tmpl      Template string containing $1, $2, etc.
 * @param uri       The original URI that was matched.
 * @param matches   Array of regmatch_t from regexec().
 * @param nmatch    Number of valid entries in matches[].
 * @param out       Output buffer for the substituted string.
 * @param out_size  Size of the output buffer.
 * @return 0 on success, -1 if output would overflow.
 */
static int substitute_backrefs(const char *tmpl, const char *uri,
                               const regmatch_t *matches, size_t nmatch,
                               char *out, size_t out_size)
{
    size_t pos = 0;
    const char *p = tmpl;

    while (*p && pos < out_size - 1) {
        if (*p == '$' && p[1] >= '0' && p[1] <= '9') {
            int idx = p[1] - '0';
            p += 2;

            if ((size_t)idx < nmatch && matches[idx].rm_so >= 0) {
                if (matches[idx].rm_eo < matches[idx].rm_so) {
                    /* Invalid capture group — skip */
                    continue;
                }
                size_t cap_len = (size_t)(matches[idx].rm_eo - matches[idx].rm_so);
                if (pos + cap_len >= out_size)
                    return -1;
                memcpy(out + pos, uri + matches[idx].rm_so, cap_len);
                pos += cap_len;
            }
            /* If index out of range, $N is simply removed */
        } else {
            out[pos++] = *p++;
        }
    }

    if (pos >= out_size)
        return -1;

    out[pos] = '\0';
    return 0;
}

int exec_redirect(lsi_session_t *session, const htaccess_directive_t *dir)
{
    int uri_len = 0;
    const char *uri;
    int status;
    int name_len;
    int val_len;

    if (!session || !dir)
        return -1;

    if (dir->type != DIR_REDIRECT)
        return -1;

    if (!dir->name)
        return -1;

    /* For non-410 (gone), target URL is required */
    if (dir->data.redirect.status_code != 410 && !dir->value)
        return -1;

    /* Get request URI */
    uri = lsi_session_get_uri(session, &uri_len);
    if (!uri || uri_len <= 0)
        return 0;

    name_len = (int)strlen(dir->name);

    /* Prefix match: URI must start with dir->name at a path segment boundary.
     * Apache mod_alias matches on path segments: Redirect /foo matches
     * /foo, /foo/, /foo/bar but NOT /foobar. */
    if (uri_len < name_len)
        return 0;
    if (memcmp(uri, dir->name, (size_t)name_len) != 0)
        return 0;
    /* After the prefix, the next char must be \0, /, or ? (query string) */
    if (uri_len > name_len) {
        char next = uri[name_len];
        if (next != '/' && next != '?' && next != '\0')
            return 0;
    }

    /* Match found — redirect via LSIAPI set_uri_qs (works in URI_MAP phase) */
    status = dir->data.redirect.status_code;
    if (status == 0)
        status = 302;

    /* For 410 (gone), no Location header — just set status */
    if (status == 410) {
        lsi_session_set_status(session, 410);
        lsi_session_end_resp(session);
        return 1;
    }

    /* Apache mod_alias appends the unmatched URI tail to the target.
     * Redirect /old /new with request /old/a/b → redirect to /new/a/b
     * If target has ?query or #fragment, insert tail before those. */
    {
        const char *tail = uri + name_len;
        int tail_len = uri_len - name_len;
        char url_buf[4096];
        int url_len;

        if (tail_len > 0) {
            /* Find ?query or #fragment in target to insert tail before it */
            const char *target = dir->value;
            const char *qf = strchr(target, '?');
            if (!qf) qf = strchr(target, '#');

            if (qf) {
                /* Insert tail between path and ?query/#fragment */
                int path_len = (int)(qf - target);
                url_len = snprintf(url_buf, sizeof(url_buf), "%.*s%.*s%s",
                                   path_len, target, tail_len, tail, qf);
            } else {
                url_len = snprintf(url_buf, sizeof(url_buf), "%s%.*s",
                                   target, tail_len, tail);
            }
        } else {
            url_len = snprintf(url_buf, sizeof(url_buf), "%s", dir->value);
        }

        if (url_len < 0 || url_len >= (int)sizeof(url_buf))
            return -1;

        /* Reject URLs containing CR/LF (header injection defense) */
        if (memchr(url_buf, '\r', url_len) || memchr(url_buf, '\n', url_len))
            return -1;

        lsi_session_redirect(session, status, url_buf, url_len);
    }

    return 1; /* Short-circuit */
}

int exec_redirect_match(lsi_session_t *session, const htaccess_directive_t *dir)
{
    int uri_len = 0;
    const char *uri;
    const char *pattern;
    regmatch_t matches[MAX_CAPTURES];
    int status;
    char url_buf[MAX_URL_LEN];
    int url_len;
    int rc;

    if (!session || !dir)
        return -1;

    if (dir->type != DIR_REDIRECT_MATCH)
        return -1;

    /* For non-410 (gone), target URL is required */
    if (dir->data.redirect.status_code != 410 && !dir->value)
        return -1;

    pattern = dir->data.redirect.pattern;
    if (!pattern)
        return -1;

    /* Reject excessively long patterns to mitigate ReDoS */
    if (strlen(pattern) > MAX_REGEX_LEN)
        return -1;

    /* Get request URI */
    uri = lsi_session_get_uri(session, &uri_len);
    if (!uri || uri_len <= 0)
        return 0;

    /* Compile regex (cached) */
    regex_t *rep = get_cached_regex(pattern, REG_EXTENDED);
    if (!rep)
        return -1; /* Invalid regex */

    /* Match against URI */
    rc = regexec(rep, uri, MAX_CAPTURES, matches, 0);
    if (rc != 0) {
        return 0; /* No match */
    }

    /* Redirect via LSIAPI set_uri_qs */
    status = dir->data.redirect.status_code;
    if (status == 0)
        status = 302;

    /* For 410 (gone), no Location header — just set status */
    if (status == 410) {
        lsi_session_set_status(session, 410);
        lsi_session_end_resp(session);
        return 1;
    }

    /* Substitute $N backreferences in the target URL template */
    if (substitute_backrefs(dir->value, uri, matches, MAX_CAPTURES,
                            url_buf, sizeof(url_buf)) != 0) {
        return -1; /* URL too long */
    }

    url_len = (int)strlen(url_buf);

    /* Reject URLs containing CR/LF (header injection defense) */
    if (memchr(url_buf, '\r', url_len) || memchr(url_buf, '\n', url_len))
        return -1;

    lsi_session_redirect(session, status, url_buf, url_len);

    return 1; /* Short-circuit */
}
