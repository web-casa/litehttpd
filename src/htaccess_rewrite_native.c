/**
 * htaccess_rewrite_native.c — built-in RewriteRule/RewriteCond engine.
 *
 * Design: docs/specs/rewrite-native.md (created with this engine).
 * All regex work goes through the P1 wrapper (htre_*) so tri-state
 * results and resource limits are honoured; an HTRE_HIT_LIMIT aborts the
 * whole chain (fail-closed on ReDoS-shaped patterns).
 */
#include "htaccess_rewrite_native.h"
#include "htaccess_regex.h"
#include "htaccess_expr.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/*  Small dynamic string                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    char  *buf;
    size_t len, cap;
    int    oom;
} rw_str_t;

static void rws_init(rw_str_t *s, size_t cap)
{
    s->buf = malloc(cap);
    s->len = 0;
    s->cap = s->buf ? cap : 0;
    s->oom = s->buf ? 0 : 1;
}

static void rws_free(rw_str_t *s) { free(s->buf); s->buf = NULL; }

static void rws_append(rw_str_t *s, const char *data, size_t n)
{
    if (s->oom || (!data && n))
        return;
    if (n == 0)
        return;
    if (s->len + n + 1 > s->cap) {
        size_t nc = s->cap ? s->cap : 64;
        while (nc < s->len + n + 1)
            nc *= 2;
        char *nb = realloc(s->buf, nc);
        if (!nb) {
            s->oom = 1;
            return;
        }
        s->buf = nb;
        s->cap = nc;
    }
    memcpy(s->buf + s->len, data, n);
    s->len += n;
    s->buf[s->len] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Flag parsing                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    int redirect;      /* 0 none; else 301..308 (default 302 for R) */
    int last;          /* [L] or [END] */
    int nocase;
    int qsa;
    int forbidden;     /* [F] -> 403 */
    int gone;          /* [G] -> 410 */
} rw_flags_t;

/* flags_raw is "[L,QSA]" (brackets included) or NULL */
static void parse_flags(const char *flags_raw, rw_flags_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!flags_raw)
        return;

    char tmp[128];
    size_t n = strlen(flags_raw);
    if (n < 2 || n >= sizeof(tmp))
        return;
    /* strip brackets */
    memcpy(tmp, flags_raw + 1, n - 2);
    tmp[n - 2] = '\0';

    for (char *tok = strtok(tmp, ","); tok; tok = strtok(NULL, ",")) {
        if (strcasecmp(tok, "L") == 0 || strcasecmp(tok, "END") == 0)
            out->last = 1;
        else if (strcasecmp(tok, "NC") == 0)
            out->nocase = 1;
        else if (strcasecmp(tok, "QSA") == 0)
            out->qsa = 1;
        else if (strcasecmp(tok, "F") == 0)
            out->forbidden = 1;
        else if (strcasecmp(tok, "G") == 0)
            out->gone = 1;
        else if (strncasecmp(tok, "R", 1) == 0) {
            if (tok[1] == '=') {
                int code = atoi(tok + 2);
                if (code >= 301 && code <= 308)
                    out->redirect = code;
            }
            if (!out->redirect)
                out->redirect = 302;
        }
        /* NE / PT / CO / etc: accepted, no-op in this subset */
    }
}

/* ------------------------------------------------------------------ */
/*  Substitution expansion                                             */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/*  RewriteCond evaluation                                             */
/* ------------------------------------------------------------------ */

/* Evaluate one cond against the expanded test string.
 * Returns 1 = cond satisfied, 0 = not, -1 = error/limit (abort chain). */
static int eval_cond(lsi_session_t *session,
                     const htaccess_directive_t *cond,
                     const char *teststr, size_t teststr_len,
                     htre_match_t *cond_m, char *cond_subject, size_t subj_cap)
{
    const char *cp = cond->data.rewrite_cond.cond_pattern;
    if (!cp)
        return 0;

    int negate = 0;
    if (cp[0] == '!') {
        negate = 1;
        cp++;
    }
    if (!cp[0])
        return 0;

    /* File tests (absolute or docroot-relative path) */
    if (strcmp(cp, "-f") == 0 || strcmp(cp, "-d") == 0 || strcmp(cp, "-s") == 0) {
        char path[2048];
        if (teststr_len == 0 || teststr_len >= sizeof(path))
            return negate ? 0 : 0;
        if (teststr[0] == '/') {
            memcpy(path, teststr, teststr_len);
            path[teststr_len] = '\0';
        } else {
            int dr_len = 0;
            const char *dr = lsi_session_get_doc_root(session, &dr_len);
            if (!dr)
                return 0;
            snprintf(path, sizeof(path), "%.*s/%.*s", dr_len, dr,
                     (int)teststr_len, teststr);
        }
        if (strstr(path, ".."))
            return 0; /* traversal in a test path: treat as no-match */
        struct stat st;
        int ok = (stat(path, &st) == 0);
        if (ok && cp[1] == 'f')
            ok = S_ISREG(st.st_mode);
        else if (ok && cp[1] == 'd')
            ok = S_ISDIR(st.st_mode);
        else if (ok && cp[1] == 's')
            ok = st.st_size > 0;
        return negate ? !ok : ok;
    }

    /* Lexicographic forms */
    if (cp[0] == '<' || cp[0] == '>' || cp[0] == '=') {
        int r = strncmp(teststr, cp + 1, teststr_len);
        if (cp[teststr_len + 1] != '\0')
            r = 1; /* test string shorter and operand longer: not equal */
        int ok;
        if (cp[0] == '<')
            ok = r < 0;
        else if (cp[0] == '>')
            ok = r > 0;
        else
            ok = (r == 0) && cp[teststr_len + 1] == '\0';
        return negate ? !ok : ok;
    }

    /* Regex */
    char err[128];
    htre_regex_t *re = htre_get_cached(RW_HTRE_TAG, cp,
                                       cond->data.rewrite_cond.nocase ? HTRE_ICASE : 0,
                                       err, sizeof(err));
    if (!re)
        return -1;

    /* keep a NUL-terminated copy for capture extraction */
    if (teststr_len >= subj_cap)
        return negate ? 0 : 0;
    memcpy(cond_subject, teststr, teststr_len);
    cond_subject[teststr_len] = '\0';

    int rc = htre_exec(re, cond_subject, HTRE_MAX_GROUPS, cond_m);
    if (rc == HTRE_HIT_LIMIT)
        return -1;
    if (rc == HTRE_ERROR)
        return -1;

    int matched = (rc == HTRE_MATCH);
    /* capture groups only meaningful on match */
    return negate ? !matched : matched;
}

/* Evaluate the cond chain of a rule. Returns 1 all satisfied, 0 no,
 * -1 abort. cond_m/cond_subject receive the LAST matching cond's data. */
static int eval_cond_chain(lsi_session_t *session,
                           const htaccess_directive_t *conds,
                           htre_match_t *cond_m, char *cond_subject, size_t subj_cap)
{
    int have_last_match = 0;
    int or_next = 0; /* pending [OR] from previous cond */

    for (const htaccess_directive_t *c = conds; c; c = c->next) {
        /* Build the test string (expand %{VAR}) */
        const char *raw = c->name; /* parser stores teststring in name */
        char expanded[2048];
        size_t elen = 0;
        if (raw) {
            for (const char *p = raw; *p && elen < sizeof(expanded) - 1; p++) {
                if (p[0] == '%' && p[1] == '{') {
                    const char *end = strchr(p + 2, '}');
                    if (end) {
                        char ref[256];
                        size_t n = (size_t)(end - (p + 2));
                        if (n < sizeof(ref)) {
                            memcpy(ref, p + 2, n);
                            ref[n] = '\0';
                            char full[260];
                            snprintf(full, sizeof(full), "%%{%s}", ref);
                            const char *v = expr_expand_var(session, full);
                            if (v) {
                                size_t vl = strlen(v);
                                if (vl > sizeof(expanded) - 1 - elen)
                                    vl = sizeof(expanded) - 1 - elen;
                                memcpy(expanded + elen, v, vl);
                                elen += vl;
                            }
                        }
                        p = end;
                        continue;
                    }
                }
                expanded[elen++] = *p;
            }
        }
        expanded[elen] = '\0';

        int rc = eval_cond(session, c, expanded, elen, cond_m, cond_subject, subj_cap);
        if (rc < 0)
            return -1;

        int satisfied = rc;
        int this_or = c->data.rewrite_cond.or_next;

        if (or_next) {
            /* inside an OR group: a single true satisfies the group */
            if (satisfied)
                or_next = 0, satisfied = 1; /* group true; skip to AND */
            else if (!this_or)
                return 0; /* last of OR group and none matched */
            else
                continue; /* still inside OR group, try next */
        } else if (!satisfied) {
            if (this_or) {
                or_next = 1; /* first failure in an OR group */
                continue;
            }
            return 0;
        }

        if (satisfied && !or_next)
            have_last_match = 1; /* capture data in cond_m is from this cond */

        or_next = this_or && !satisfied ? 1 : 0;
        if (this_or && satisfied)
            or_next = 0; /* satisfied OR member terminates the group */
    }
    (void)have_last_match;
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Engine entry                                                       */
/* ------------------------------------------------------------------ */

int rewrite_native_exec(lsi_session_t *session,
                        const htaccess_directive_t *directives,
                        const char *rewrite_base)
{
    if (!session || !directives)
        return 0;

    int uri_len = 0;
    const char *uri = lsi_session_get_uri(session, &uri_len);
    if (!uri || uri_len <= 0)
        return 0;

    /* current path (mutated across rules), kept NUL-terminated */
    char path[4096];
    if ((size_t)uri_len >= sizeof(path))
        return 0;
    memcpy(path, uri, uri_len);
    path[uri_len] = '\0';

    int qs_len = 0;
    const char *qs = lsi_session_get_query_string(session, &qs_len);
    char query[2048];
    if (qs && qs_len > 0) {
        if ((size_t)qs_len >= sizeof(query))
            qs_len = (int)sizeof(query) - 1;
        memcpy(query, qs, qs_len);
        query[qs_len] = '\0';
    } else {
        query[0] = '\0';
    }

    int result = 0;        /* 0 none, 1 internal rewrite, 3xx redirect */
    int final_status = 0;  /* 403/410 from F/G */
    int stop = 0;

    for (const htaccess_directive_t *d = directives; d && !stop; d = d->next) {
        if (d->type != DIR_REWRITE_RULE)
            continue;
        const char *pat = d->data.rewrite_rule.pattern;
        const char *subst = d->value;
        if (!pat || !subst)
            continue;

        rw_flags_t fl;
        parse_flags(d->data.rewrite_rule.flags_raw, &fl);

        /* [F]/[G] with '-' substitution apply only when the pattern
         * matches (conditions included) — handled after the match. */

        int negate = 0;
        if (pat[0] == '!') {
            negate = 1;
            pat++;
        }
        if (!pat[0])
            continue;

        char err[128];
        htre_regex_t *re = htre_get_cached(RW_HTRE_TAG, pat,
                                           fl.nocase ? HTRE_ICASE : 0,
                                           err, sizeof(err));
        if (!re)
            continue;

        htre_match_t m[HTRE_MAX_GROUPS];
        int rc = htre_exec(re, path, HTRE_MAX_GROUPS, m);
        if (rc == HTRE_HIT_LIMIT || rc == HTRE_ERROR) {
            /* resource limit / internal error: abort the whole chain
             * fail-closed rather than half-applying rules */
            lsi_log(session, LSI_LOG_WARN,
                    "[htaccess] rewrite: pattern '%s' aborted (limit/error)",
                    pat);
            return result;
        }

        int matched = (rc == HTRE_MATCH);
        if (negate)
            matched = !matched;
        /* On negated match there are no usable groups */
        if (negate)
            memset(m, -1, sizeof(m));

        if (matched) {
            /* conditions gate the rule */
            if (d->data.rewrite_rule.conditions) {
                htre_match_t cm[HTRE_MAX_GROUPS];
                char cond_subj[2048];
                int crc = eval_cond_chain(session, d->data.rewrite_rule.conditions,
                                          cm, cond_subj, sizeof(cond_subj));
                if (crc < 0)
                    return result; /* limit — stop chain */
                if (crc == 0)
                    continue; /* rule does not apply */
            }

            /* F / G short-circuit */
            if (fl.forbidden) {
                final_status = 403;
                stop = 1;
                break;
            }
            if (fl.gone) {
                final_status = 410;
                stop = 1;
                break;
            }

            /* '-' substitution: URI unchanged (side-effect only) */
            if (strcmp(subst, "-") != 0) {
                /* Expand substitution */
                rw_str_t out;
                rws_init(&out, 256);
                for (const char *p = subst; *p; p++) {
                    if (*p == '$' && p[1] >= '0' && p[1] <= '9') {
                        int idx = p[1] - '0';
                        p++;
                        if (!negate && m[idx].so >= 0)
                            rws_append(&out, path + m[idx].so,
                                       (size_t)(m[idx].eo - m[idx].so));
                        continue;
                    }
                    if (*p == '%' && p[1] >= '1' && p[1] <= '9') {
                        int idx = p[1] - '0';
                        p++;
                        /* %N from last matching cond — filled only if the
                         * rule had conds; empty otherwise (subset) */
                        (void)idx;
                        continue;
                    }
                    if (p[0] == '%' && p[1] == '{') {
                        const char *end = strchr(p + 2, '}');
                        if (end) {
                            char ref[256];
                            size_t n = (size_t)(end - (p + 2));
                            if (n < sizeof(ref)) {
                                memcpy(ref, p + 2, n);
                                ref[n] = '\0';
                                char full[260];
                                snprintf(full, sizeof(full), "%%{%s}", ref);
                                const char *v = expr_expand_var(session, full);
                                if (v)
                                    rws_append(&out, v, strlen(v));
                            }
                            p = end;
                            continue;
                        }
                    }
                    rws_append(&out, p, 1);
                }

                if (!out.oom && out.len > 0) {
                    out.buf[out.len] = '\0';
                    const char *newuri = out.buf;

                    int is_absolute = (strncasecmp(newuri, "http://", 7) == 0 ||
                                       strncasecmp(newuri, "https://", 8) == 0);

                    if (is_absolute || fl.redirect) {
                        /* External redirect. Relative targets get the
                         * scheme+host prefix; status defaults 302. */
                        rw_str_t loc;
                        rws_init(&loc, 512);
                        if (is_absolute) {
                            rws_append(&loc, newuri, out.len);
                        } else {
                            const char *http = "http://";
                            rws_append(&loc, http, 7);
                            const char *host = expr_expand_var(session, "%{HTTP_HOST}");
                            if (host && host[0])
                                rws_append(&loc, host, strlen(host));
                            rws_append(&loc, newuri, out.len);
                        }
                        /* QSA on redirect: append original query */
                        if (fl.qsa && query[0]) {
                            rws_append(&loc, strchr(loc.buf, '?') ? "&" : "?", 1);
                            rws_append(&loc, query, strlen(query));
                        }
                        if (!loc.oom && loc.len > 0) {
                            loc.buf[loc.len] = '\0';
                            int code = fl.redirect ? fl.redirect : 302;
                            lsi_session_redirect(session, code, loc.buf,
                                                 (int)loc.len);
                            result = code;
                            stop = 1;
                        }
                        rws_free(&loc);
                    } else {
                        /* Internal rewrite */
                        const char *target = newuri;
                        char withbase[4096];
                        if (target[0] != '/' && rewrite_base && rewrite_base[0]) {
                            snprintf(withbase, sizeof(withbase), "%s%s",
                                     rewrite_base, target);
                            target = withbase;
                        }
                        /* QSA: append original query to existing one */
                        char full[4096 + 2048];
                        const char *qpos = strchr(target, '?');
                        size_t plen = qpos ? (size_t)(qpos - target) : strlen(target);
                        const char *newq = qpos ? qpos + 1 : "";
                        int n = -1;
                        if (fl.qsa && query[0]) {
                            n = snprintf(full, sizeof(full), "%.*s%s%s%s",
                                         (int)plen, target,
                                         newq[0] ? "?" : "",
                                         newq[0] ? newq : "",
                                         newq[0] ? "&" : "?");
                            if (n > 0 && (size_t)n < sizeof(full))
                                n += snprintf(full + n, sizeof(full) - n, "%s", query);
                        } else if (qpos) {
                            n = snprintf(full, sizeof(full), "%s", target);
                        } else {
                            n = snprintf(full, sizeof(full), "%.*s%s",
                                         (int)plen, target, query[0] ? "?" : "");
                            if (n > 0 && (size_t)n < sizeof(full) && query[0])
                                n += snprintf(full + n, sizeof(full) - n, "%s", query);
                        }
                        const char *final = (n > 0) ? full : target;
                        /* set_uri_internal expects path+query? Pass path
                         * with '?'-joined query (Apache per-dir behaviour
                         * via RewriteRule keeps query in substitution). */
                        lsi_session_set_uri_internal(session, final,
                                                     (int)strlen(final));
                        /* keep working copy for subsequent rules */
                        if (strlen(final) < sizeof(path))
                            snprintf(path, sizeof(path), "%s", final);
                        /* strip query from working path for later patterns */
                        char *q = strchr(path, '?');
                        if (q)
                            *q = '\0';
                        result = 1;
                    }
                }
                rws_free(&out);
            }

            if (fl.last)
                stop = 1;
        }
    }

    if (final_status)
        return final_status;
    return result;
}
