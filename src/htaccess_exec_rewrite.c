/**
 * htaccess_exec_rewrite.c - RewriteRule/RewriteCond execution via OLS engine
 *
 * Delegates rewrite processing to OLS's native RewriteEngine by:
 * 1. Checking RewriteEngine On/Off
 * 2. Rebuilding RewriteCond/Rule text from parsed directives
 * 3. Calling g_api->parse_rewrite_rules() to get an opaque handle
 * 4. Calling g_api->exec_rewrite_rules() to execute against the session
 *
 * On stock OLS (no rewrite patch), gracefully returns -1.
 */
#include "htaccess_exec_rewrite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/** Initial buffer size for text reconstruction. */
#define REWRITE_TEXT_INIT_SIZE 2048

/** Maximum rewrite text size to prevent abuse. */
#define REWRITE_TEXT_MAX_SIZE  65536

/* ------------------------------------------------------------------ */
/*  Rewrite handle cache — avoids re-parsing on every request          */
/* ------------------------------------------------------------------ */

/**
 * Single-entry thread-local cache for parsed rewrite rules.
 * Uses a lightweight fingerprint (rule count + first pattern pointer hash)
 * to detect changes without rebuilding the full text on every request.
 */
static __thread struct {
    unsigned long fingerprint; /* lightweight identity check */
    void         *handle;     /* opaque handle from parse_rewrite_rules */
    int           valid;      /* 1 if cache entry is populated */
} rw_cache = { 0, NULL, 0 };

/**
 * Compute a lightweight fingerprint of rewrite directives.
 * Much cheaper than rebuilding the full text — only counts rules
 * and hashes first pattern pointer + first cond pointer.
 */
static unsigned long rewrite_fingerprint(const htaccess_directive_t *directives)
{
    unsigned long fp = 5381;
    int count = 0;
    for (const htaccess_directive_t *d = directives; d; d = d->next) {
        if (d->type == DIR_REWRITE_RULE) {
            count++;
            if (d->data.rewrite_rule.pattern)
                for (const char *p = d->data.rewrite_rule.pattern; *p; p++)
                    fp = ((fp << 5) + fp) + (unsigned char)*p;
            if (d->value)
                for (const char *p = d->value; *p; p++)
                    fp = ((fp << 5) + fp) + (unsigned char)*p;
            /* Include flags so [L] → [R=301,L] invalidates cache */
            if (d->data.rewrite_rule.flags_raw)
                for (const char *p = d->data.rewrite_rule.flags_raw; *p; p++)
                    fp = ((fp << 5) + fp) + (unsigned char)*p;
            /* Include attached RewriteCond content */
            for (const htaccess_directive_t *c = d->data.rewrite_rule.conditions;
                 c; c = c->next) {
                if (c->data.rewrite_cond.cond_pattern)
                    for (const char *p = c->data.rewrite_cond.cond_pattern; *p; p++)
                        fp = ((fp << 5) + fp) + (unsigned char)*p;
                /* Include cond test string (stored in name) */
                if (c->name)
                    for (const char *p = c->name; *p; p++)
                        fp = ((fp << 5) + fp) + (unsigned char)*p;
                /* Include raw flags for complete identity */
                if (c->data.rewrite_cond.flags_raw)
                    for (const char *p = c->data.rewrite_cond.flags_raw; *p; p++)
                        fp = ((fp << 5) + fp) + (unsigned char)*p;
                else {
                    fp = ((fp << 5) + fp) + (unsigned)c->data.rewrite_cond.nocase;
                    fp = ((fp << 5) + fp) + (unsigned)c->data.rewrite_cond.or_next;
                }
            }
        } else if (d->type == DIR_REWRITE_BASE && d->value) {
            for (const char *p = d->value; *p; p++)
                fp = ((fp << 5) + fp) + (unsigned char)*p;
        } else if (d->type == DIR_REWRITE_OPTIONS && d->value) {
            for (const char *p = d->value; *p; p++)
                fp = ((fp << 5) + fp) + (unsigned char)*p;
        } else if (d->type == DIR_REWRITE_MAP) {
            if (d->data.rewrite_map.map_name)
                for (const char *p = d->data.rewrite_map.map_name; *p; p++)
                    fp = ((fp << 5) + fp) + (unsigned char)*p;
            if (d->data.rewrite_map.map_type)
                for (const char *p = d->data.rewrite_map.map_type; *p; p++)
                    fp = ((fp << 5) + fp) + (unsigned char)*p;
            if (d->data.rewrite_map.map_source)
                for (const char *p = d->data.rewrite_map.map_source; *p; p++)
                    fp = ((fp << 5) + fp) + (unsigned char)*p;
        }
    }
    fp = ((fp << 5) + fp) + (unsigned)count;
    return fp;
}

/* ------------------------------------------------------------------ */
/*  Text buffer helper                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    char  *buf;
    int    len;
    int    cap;
} textbuf_t;

static int textbuf_init(textbuf_t *tb, int cap)
{
    tb->buf = (char *)malloc((size_t)cap);
    if (!tb->buf) return -1;
    tb->len = 0;
    tb->cap = cap;
    return 0;
}

static int textbuf_append(textbuf_t *tb, const char *s, int slen)
{
    if (slen <= 0) return 0;
    if (slen > REWRITE_TEXT_MAX_SIZE - tb->len) return -1; /* overflow guard */
    if (tb->len + slen >= tb->cap) {
        int newcap = (tb->cap * 2 > tb->len + slen + 1)
                     ? tb->cap * 2
                     : tb->len + slen + 256;
        if (newcap > REWRITE_TEXT_MAX_SIZE) return -1;
        char *nb = (char *)realloc(tb->buf, (size_t)newcap);
        if (!nb) return -1;
        tb->buf = nb;
        tb->cap = newcap;
    }
    memcpy(tb->buf + tb->len, s, (size_t)slen);
    tb->len += slen;
    tb->buf[tb->len] = '\0';
    return 0;
}

static int textbuf_append_str(textbuf_t *tb, const char *s)
{
    return s ? textbuf_append(tb, s, (int)strlen(s)) : 0;
}

/* ------------------------------------------------------------------ */
/*  Rebuild rewrite text from parsed directives                        */
/* ------------------------------------------------------------------ */

/**
 * Append a single RewriteCond line to the text buffer.
 */
static int append_cond_text(textbuf_t *tb, const htaccess_directive_t *cond)
{
    if (textbuf_append_str(tb, "RewriteCond ") != 0) return -1;
    if (textbuf_append_str(tb, cond->name) != 0) return -1;
    if (textbuf_append_str(tb, " ") != 0) return -1;
    if (textbuf_append_str(tb, cond->data.rewrite_cond.cond_pattern) != 0) return -1;

    /* Use stored raw flags for lossless pass-through;
     * fallback to reconstructing from parsed booleans */
    if (cond->data.rewrite_cond.flags_raw) {
        if (textbuf_append_str(tb, " ") != 0) return -1;
        if (textbuf_append_str(tb, cond->data.rewrite_cond.flags_raw) != 0)
            return -1;
    } else if (cond->data.rewrite_cond.nocase || cond->data.rewrite_cond.or_next) {
        if (textbuf_append_str(tb, " [") != 0) return -1;
        int need_comma = 0;
        if (cond->data.rewrite_cond.nocase) {
            if (textbuf_append_str(tb, "NC") != 0) return -1;
            need_comma = 1;
        }
        if (cond->data.rewrite_cond.or_next) {
            if (need_comma && textbuf_append_str(tb, ",") != 0) return -1;
            if (textbuf_append_str(tb, "OR") != 0) return -1;
        }
        if (textbuf_append_str(tb, "]") != 0) return -1;
    }
    if (textbuf_append_str(tb, "\n") != 0) return -1;
    return 0;
}

char *rebuild_rewrite_text(const htaccess_directive_t *directives, int *out_len)
{
    textbuf_t tb;
    if (textbuf_init(&tb, REWRITE_TEXT_INIT_SIZE) != 0) {
        *out_len = 0;
        return NULL;
    }

    for (const htaccess_directive_t *d = directives; d; d = d->next) {
        /* Emit RewriteBase so OLS engine knows the base path */
        if (d->type == DIR_REWRITE_BASE && d->value) {
            if (textbuf_append_str(&tb, "RewriteBase ") != 0) goto fail;
            if (textbuf_append_str(&tb, d->value) != 0) goto fail;
            if (textbuf_append_str(&tb, "\n") != 0) goto fail;
        }

        /* Emit RewriteOptions so OLS engine sees them */
        if (d->type == DIR_REWRITE_OPTIONS && d->value) {
            if (textbuf_append_str(&tb, "RewriteOptions ") != 0) goto fail;
            if (textbuf_append_str(&tb, d->value) != 0) goto fail;
            if (textbuf_append_str(&tb, "\n") != 0) goto fail;
        }

        /* Emit RewriteMap definitions */
        if (d->type == DIR_REWRITE_MAP && d->data.rewrite_map.map_name) {
            if (textbuf_append_str(&tb, "RewriteMap ") != 0) goto fail;
            if (textbuf_append_str(&tb, d->data.rewrite_map.map_name) != 0) goto fail;
            if (d->data.rewrite_map.map_type) {
                if (textbuf_append_str(&tb, " ") != 0) goto fail;
                if (textbuf_append_str(&tb, d->data.rewrite_map.map_type) != 0) goto fail;
                if (d->data.rewrite_map.map_source) {
                    if (textbuf_append_str(&tb, ":") != 0) goto fail;
                    if (textbuf_append_str(&tb, d->data.rewrite_map.map_source) != 0) goto fail;
                }
            }
            if (textbuf_append_str(&tb, "\n") != 0) goto fail;
        }

        if (d->type == DIR_REWRITE_RULE) {
            /* Print associated conditions first */
            for (const htaccess_directive_t *cond = d->data.rewrite_rule.conditions;
                 cond; cond = cond->next) {
                if (append_cond_text(&tb, cond) != 0) goto fail;
            }

            /* Print RewriteRule */
            if (textbuf_append_str(&tb, "RewriteRule ") != 0) goto fail;
            if (textbuf_append_str(&tb, d->data.rewrite_rule.pattern) != 0) goto fail;
            if (textbuf_append_str(&tb, " ") != 0) goto fail;
            if (textbuf_append_str(&tb, d->value) != 0) goto fail;
            if (d->data.rewrite_rule.flags_raw) {
                if (textbuf_append_str(&tb, " ") != 0) goto fail;
                if (textbuf_append_str(&tb, d->data.rewrite_rule.flags_raw) != 0) goto fail;
            }
            if (textbuf_append_str(&tb, "\n") != 0) goto fail;
        }
    }

    if (tb.len == 0) {
        free(tb.buf);
        *out_len = 0;
        return NULL;
    }

    *out_len = tb.len;
    return tb.buf;

fail:
    free(tb.buf);
    *out_len = 0;
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Main execution entry point                                         */
/* ------------------------------------------------------------------ */

int exec_rewrite_rules(lsi_session_t *session,
                       const htaccess_directive_t *directives)
{
    /* 1. Check RewriteEngine On/Off */
    /* Handles: DIR_REWRITE_ENGINE, DIR_REWRITE_BASE, DIR_REWRITE_COND, DIR_REWRITE_RULE */
    int engine_on = 0;
    const char *rewrite_base = NULL;
    int base_len = 0;

    for (const htaccess_directive_t *d = directives; d; d = d->next) {
        if (d->type == DIR_REWRITE_ENGINE)
            engine_on = (d->name && strcasecmp(d->name, "On") == 0);
        else if (d->type == DIR_REWRITE_BASE) {
            rewrite_base = d->value;
            base_len = rewrite_base ? (int)strlen(rewrite_base) : 0;
        }
    }

    if (!engine_on)
        return 0;

    /* 2. Check g_api has rewrite support (requires custom OLS) */
    if (!g_api || !g_api->parse_rewrite_rules ||
        !g_api->exec_rewrite_rules || !g_api->free_rewrite_rules) {
        /* Capability already logged at module init — skip silently */
        return -1;
    }

    /* 3. Check handle cache via lightweight fingerprint */
    unsigned long fp = rewrite_fingerprint(directives);

    if (rw_cache.valid && rw_cache.fingerprint == fp) {
        /* Cache hit — skip text rebuild and re-parse */
        int rc = g_api->exec_rewrite_rules(session, rw_cache.handle,
                                            rewrite_base, base_len);
        if (rc == 1) return 1;
        if (rc >= 301 && rc <= 399) return rc;  /* Redirect with status */
        if (rc == 403 || rc == 410) return rc;
        return 0;
    }

    /* Cache miss — rebuild text and parse */
    int text_len = 0;
    char *text = rebuild_rewrite_text(directives, &text_len);
    if (!text || text_len == 0)
        return 0;

    void *handle = g_api->parse_rewrite_rules(text, text_len);
    free(text);
    if (!handle)
        return 0;

    /* Update cache */
    if (rw_cache.valid && rw_cache.handle)
        g_api->free_rewrite_rules(rw_cache.handle);
    rw_cache.fingerprint = fp;
    rw_cache.handle = handle;
    rw_cache.valid = 1;

    /* 4. Execute rules against current session */
    int rc = g_api->exec_rewrite_rules(session, handle, rewrite_base, base_len);

    /* 5. Interpret result (handle stays cached, not freed per-request) */
    if (rc == 1)
        return 1;    /* URI rewritten internally */
    if (rc >= 301 && rc <= 399)
        return rc;   /* External redirect — status code preserved */
    if (rc == 403 || rc == 410)
        return rc;   /* Forbidden or Gone */

    return 0;        /* No match */
}

void rewrite_cache_cleanup(void)
{
    if (rw_cache.valid && rw_cache.handle) {
        if (g_api && g_api->free_rewrite_rules)
            g_api->free_rewrite_rules(rw_cache.handle);
        rw_cache.handle = NULL;
        rw_cache.valid = 0;
        rw_cache.fingerprint = 0;
    }
}
