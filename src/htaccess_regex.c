/**
 * htaccess_regex.c — Unified PCRE2 regex wrapper implementation.
 * Spec: docs/specs/regex-engine.md
 */
#include "htaccess_regex.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HTRE_CACHE_SLOTS   16
#define HTRE_PAT_MAX       512
#define HTRE_SUBJECT_MAX   8192
#define HTRE_MATCH_LIMIT   100000
#define HTRE_DEPTH_LIMIT   1000

/* Limit values match the pre-existing FilesMatch baseline
 * (htaccess_exec_files_match.c) so all executors share one policy table. */

struct htre_regex {
    pcre2_code       *code;
    pcre2_match_context *mctx;   /* owns the resource limits */
    char             *pattern;
    uint32_t          flags;
    uint64_t          hash;
    uint64_t          used;      /* LRU counter */
};

typedef struct {
    htre_regex_t *slots[HTRE_CACHE_SLOTS];
    size_t        count;
    uint64_t      clock;
} htre_cache_t;

/* One cache per tag; tags are small ints assigned per executor. */
#define HTRE_MAX_TAGS 8
static __thread htre_cache_t g_caches[HTRE_MAX_TAGS];
static __thread int g_init_done = 0;

static void htre_init_once(void)
{
    if (!g_init_done) {
        memset(g_caches, 0, sizeof(g_caches));
        g_init_done = 1;
    }
}

static uint64_t fnv1a(const char *s)
{
    uint64_t h = 1469598103934665603ULL;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

static void htre_free_one(htre_regex_t *re)
{
    if (!re)
        return;
    if (re->code)
        pcre2_code_free(re->code);
    if (re->mctx)
        pcre2_match_context_free(re->mctx);
    free(re->pattern);
    free(re);
}

htre_regex_t *htre_get_cached(int tag, const char *pattern, int flags,
                              char *errbuf, size_t errlen)
{
    if (tag < 0 || tag >= HTRE_MAX_TAGS || !pattern)
        return NULL;
    htre_init_once();
    htre_cache_t *c = &g_caches[tag];

    if (strlen(pattern) > HTRE_PAT_MAX) {
        if (errbuf && errlen)
            snprintf(errbuf, errlen, "pattern exceeds %d bytes", HTRE_PAT_MAX);
        return NULL;
    }

    uint64_t h = fnv1a(pattern) ^ (uint32_t)flags;
    for (size_t i = 0; i < HTRE_CACHE_SLOTS; i++) {
        htre_regex_t *re = c->slots[i];
        if (re && re->hash == h && re->flags == (uint32_t)flags &&
            strcmp(re->pattern, pattern) == 0) {
            re->used = ++c->clock;
            return re;
        }
    }

    /* compile */
    int errcode = 0;
    PCRE2_SIZE erroff = 0;
    uint32_t pflags = PCRE2_DOTALL;   /* Apache rewrite semantics: '.' matches NL */
    if (flags & 0x1)                   /* HTRE_ICASE */
        pflags |= PCRE2_CASELESS;

    pcre2_code *code = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED,
                                     pflags, &errcode, &erroff, NULL);
    if (!code) {
        if (errbuf && errlen)
            pcre2_get_error_message(errcode, (PCRE2_UCHAR *)errbuf, errlen);
        return NULL;
    }

    pcre2_match_context *mctx = pcre2_match_context_create(NULL);
    if (!mctx) {
        pcre2_code_free(code);
        return NULL;
    }
    pcre2_set_match_limit(mctx, HTRE_MATCH_LIMIT);
    pcre2_set_depth_limit(mctx, HTRE_DEPTH_LIMIT);

    htre_regex_t *re = calloc(1, sizeof(*re));
    if (!re) {
        pcre2_match_context_free(mctx);
        pcre2_code_free(code);
        return NULL;
    }
    re->code = code;
    re->mctx = mctx;
    re->pattern = strdup(pattern);
    if (!re->pattern) {
        htre_free_one(re);
        return NULL;
    }
    re->flags = flags;
    re->hash = h;
    re->used = ++c->clock;

    /* insert with simple LRU eviction */
    size_t victim = 0;
    if (c->count < HTRE_CACHE_SLOTS) {
        for (size_t i = 0; i < HTRE_CACHE_SLOTS; i++) {
            if (!c->slots[i]) {
                victim = i;
                break;
            }
        }
    } else {
        uint64_t oldest = UINT64_MAX;
        for (size_t i = 0; i < HTRE_CACHE_SLOTS; i++) {
            if (c->slots[i] && c->slots[i]->used < oldest) {
                oldest = c->slots[i]->used;
                victim = i;
            }
        }
        htre_free_one(c->slots[victim]);
    }
    c->slots[victim] = re;
    if (c->count < HTRE_CACHE_SLOTS)
        c->count++;
    return re;
}

int htre_exec(const htre_regex_t *re, const char *subject,
              size_t nmatch, htre_match_t *pmatch)
{
    if (!re || !subject)
        return HTRE_ERROR;
    size_t len = strlen(subject);
    if (len > HTRE_SUBJECT_MAX)
        return HTRE_ERROR;

    size_t ovecsize = (nmatch ? nmatch : 1) * 2;
    if (nmatch > HTRE_MAX_GROUPS)
        nmatch = HTRE_MAX_GROUPS;

    pcre2_match_data *md = pcre2_match_data_create(ovecsize, NULL);
    if (!md)
        return HTRE_ERROR;

    int rc = pcre2_match(re->code, (PCRE2_SPTR)subject, len, 0, 0,
                         md, re->mctx);
    if (rc == PCRE2_ERROR_NOMATCH) {
        pcre2_match_data_free(md);
        return HTRE_NO_MATCH;
    }
    if (rc == PCRE2_ERROR_MATCHLIMIT || rc == PCRE2_ERROR_DEPTHLIMIT ||
        rc == PCRE2_ERROR_RECURSIONLIMIT) {
        pcre2_match_data_free(md);
        return HTRE_HIT_LIMIT;
    }
    if (rc < 0) {
        pcre2_match_data_free(md);
        return HTRE_ERROR;
    }

    if (pmatch && nmatch) {
        PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
        uint32_t got = pcre2_get_ovector_count(md);
        for (size_t g = 0; g < nmatch; g++) {
            if ((uint32_t)g < got && ov[g * 2] != PCRE2_UNSET) {
                pmatch[g].so = (int)ov[g * 2];
                pmatch[g].eo = (int)ov[g * 2 + 1];
            } else {
                pmatch[g].so = pmatch[g].eo = -1;
            }
        }
    }
    pcre2_match_data_free(md);
    return HTRE_MATCH;
}

void htre_thread_cleanup(void)
{
    if (!g_init_done)
        return;
    for (int t = 0; t < HTRE_MAX_TAGS; t++) {
        htre_cache_t *c = &g_caches[t];
        for (size_t i = 0; i < HTRE_CACHE_SLOTS; i++) {
            htre_free_one(c->slots[i]);
            c->slots[i] = NULL;
        }
        c->count = 0;
    }
    g_init_done = 0;
}
