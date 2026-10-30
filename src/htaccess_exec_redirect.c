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
#include "htaccess_regex.h"

/** Maximum number of regex capture groups supported. */
#define MAX_CAPTURES 10

/** Maximum length of the substituted Location URL. */
#define MAX_URL_LEN 4096

/** Maximum regex pattern length to prevent ReDoS. */
#define MAX_REGEX_LEN 512

/* Regex operations migrated to the unified PCRE2 wrapper.
 * Spec: docs/specs/regex-engine.md. HTRE_HIT_LIMIT is never silently
 * treated as NO_MATCH here (fail-open defect fixed). */
#define REDIR_REGEX_TAG 0

/**
 * Substitute $N backreferences in a template string with captured values.
 *
 * @param tmpl      Template string containing $1, $2, etc.
 * @param uri       The original URI that was matched.
 * @param matches   Array of htre_match_t from htre_exec().
 * @param nmatch    Number of valid entries in matches[].
 * @param out       Output buffer for the substituted string.
 * @param out_size  Size of the output buffer.
 * @return 0 on success, -1 if output would overflow.
 */
static int substitute_backrefs(const char *tmpl, const char *uri,
                               const htre_match_t *matches, size_t nmatch,
                               char *out, size_t out_size)
{
    size_t pos = 0;
    const char *p = tmpl;

    while (*p && pos < out_size - 1) {
        if (*p == '$' && p[1] >= '0' && p[1] <= '9') {
            int idx = p[1] - '0';
            p += 2;

            if ((size_t)idx < nmatch && matches[idx].so >= 0) {
                if (matches[idx].eo < matches[idx].so) {
                    /* Invalid capture group — skip */
                    continue;
                }
                size_t cap_len = (size_t)(matches[idx].eo - matches[idx].so);
                if (pos + cap_len >= out_size)
                    return -1;
                memcpy(out + pos, uri + matches[idx].so, cap_len);
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
    htre_match_t matches[MAX_CAPTURES];
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

    /* Compile regex (cached, resource-limited PCRE2) */
    htre_regex_t *rep = htre_get_cached(REDIR_REGEX_TAG, pattern, 0, NULL, 0);
    if (!rep)
        return -1; /* Invalid regex */

    /* Match against URI; HIT_LIMIT/ERROR must not look like "no match". */
    rc = htre_exec(rep, uri, MAX_CAPTURES, matches);
    if (rc == HTRE_HIT_LIMIT || rc == HTRE_ERROR) {
        lsi_log(session, LSI_LOG_WARN,
                "[htaccess] RedirectMatch: regex resource limit hit for %s, "
                "rule skipped", pattern);
        return 0;
    }
    if (rc != HTRE_MATCH) {
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
