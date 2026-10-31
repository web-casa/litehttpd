/**
 * htaccess_esi.c — ESI v1 tag state machine (remove/comment/vars).
 * Spec: docs/specs/esi-v1.md
 */
#include "htaccess_esi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ESI_TAG_PREFIX    "<esi:"
#define ESI_TAG_PREFIX_LEN 5
#define ESI_REMOVE_CLOSE  "</esi:remove>"
#define ESI_COMMENT_CLOSE "</esi:comment>"
#define ESI_VARS_CLOSE    "</esi:vars>"

void esi_ctx_init(esi_ctx_t *ctx, const char *docroot, size_t docroot_len)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = ESI_STATE_PASSTHROUGH;
    ctx->buf_cap = 16384;
    ctx->buf = malloc(ctx->buf_cap);
    /* Set after the memset: NULL/0 docroot disables <esi:include>
     * (fail closed — resolving against "/" would be arbitrary file read). */
    if (docroot && docroot_len > 0) {
        ctx->docroot = docroot;
        ctx->docroot_len = docroot_len;
    }
}

void esi_ctx_free(esi_ctx_t *ctx)
{
    free(ctx->buf);
    ctx->buf = NULL;
    ctx->buf_len = 0;
}

#include <stdio.h>
static int buf_append(esi_ctx_t *ctx, const char *data, size_t len)
{
    if (ctx->buf_len + len > ctx->buf_cap) {
        size_t new_cap = ctx->buf_cap * 2;
        while (new_cap < ctx->buf_len + len)
            new_cap *= 2;
        if (new_cap > ESI_MAX_BUFFER)
            return -1;
        char *nb = realloc(ctx->buf, new_cap);
        if (!nb)
            return -1;
        ctx->buf = nb;
        ctx->buf_cap = new_cap;
    }
    memcpy(ctx->buf + ctx->buf_len, data, len);
    ctx->buf_len += len;
    return 0;
}

int esi_content_has_tags(const char *content, size_t len)
{
    if (!content || len < ESI_TAG_PREFIX_LEN)
        return 0;
    /* Boyer-Moore-lite: scan for '<esi:' */
    for (size_t i = 0; i + ESI_TAG_PREFIX_LEN <= len; i++) {
        if (content[i] == '<' &&
            memcmp(content + i, ESI_TAG_PREFIX, ESI_TAG_PREFIX_LEN) == 0)
            return 1;
    }
    return 0;
}

static const char *find_str(const char *hay, size_t hay_len, const char *needle)
{
    size_t nlen = strlen(needle);
    if (hay_len < nlen)
        return NULL;
    for (size_t i = 0; i + nlen <= hay_len; i++) {
        if (hay[i] == needle[0] && memcmp(hay + i, needle, nlen) == 0)
            return hay + i;
    }
    return NULL;
}

/* Defined below the state machine; forward-declared for the INCLUDE path. */
static char *esi_extract_src(const char *tag_start, size_t tag_len);
static int esi_src_is_handler(const char *path);

/* Dynamic-include placeholder: \x01LHE<i32le>\x02 — 8 bytes, collision-safe
 * in text bodies; spliced at EOF by esi_dyn_splice. */
#define ESI_PH_LEN 8
static void emit_placeholder(esi_ctx_t *ctx, int idx)
{
    unsigned char ph[ESI_PH_LEN] = {0x01, 'L', 'H', 'E',
                                     0, 0, 0, 0, };
    ph[4] = (unsigned char)(idx & 0xff);
    ph[5] = (unsigned char)((idx >> 8) & 0xff);
    ph[6] = (unsigned char)((idx >> 16) & 0xff);
    ph[7] = 0x02;
    buf_append(ctx, (const char *)ph, ESI_PH_LEN);
}

/* Chunk-boundary hold-back: a tag split across body chunks must never leak
 * to the client as literal text. The context retains a possible partial
 * tag (<esi: prefix or an unterminated include) and re-presents it
 * prepended to the next chunk. Longer partials fail open: emitted
 * literally so a hostile page cannot make us buffer without bound. */
#define ESI_HOLD_MAX 8192

/* Longest k (1..4) such that the last k bytes of data are a proper prefix
 * of "<esi:". 0 = no partial tag at the end. */
static size_t partial_tag_tail(const char *data, size_t len)
{
    static const char pfx[] = "<esi:";
    size_t max_k = (len < 4) ? len : 4;
    for (size_t k = max_k; k >= 1; k--) {
        if (memcmp(data + len - k, pfx, k) == 0)
            return k;
    }
    return 0;
}

static void hold_bytes(esi_ctx_t *ctx, const char *data, size_t len)
{
    if (len > ESI_HOLD_MAX)
        len = ESI_HOLD_MAX; /* caller checks; defensive */
    memcpy(ctx->hold, data, len);
    ctx->hold_len = len;
}

size_t esi_process(esi_ctx_t *ctx, const char *input, size_t input_len,
                   char **out_buf, size_t *out_len)
{
    if (!ctx || !input || !out_buf || !out_len)
        return 0;

    /* Prepend any held-back partial tag from the previous chunk so tags
     * split across body chunks are seen whole. */
    size_t prev_hold = ctx->hold_len;
    char *work = NULL;
    if (prev_hold > 0) {
        work = malloc(prev_hold + input_len);
        if (!work) {
            /* OOM: drop the held bytes, process this chunk alone */
            ctx->hold_len = 0;
            prev_hold = 0;
        } else {
            memcpy(work, ctx->hold, prev_hold);
            memcpy(work + prev_hold, input, input_len);
            ctx->hold_len = 0;
        }
    }
    const char *data = work ? work : input;
    size_t data_len = work ? prev_hold + input_len : input_len;

    *out_len = 0;
    *out_buf = NULL;
    size_t consumed = 0;
    /* Use ctx->buf as output accumulation too (simpler than separate buffer) */
    size_t out_start = 0; /* offset in ctx->buf where output starts */
    /* Pending output parked by the caller (backpressure) must be drained
     * before this call reuses the buffer — refuse rather than clobber. */
    if (ctx->pend_len > 0) {
        *out_buf = ctx->buf;
        *out_len = ctx->pend_len;
        return 0; /* caller: drain first, then re-invoke */
    }
    ctx->buf_len = 0; /* clear for this chunk's output */

    while (consumed < data_len) {
        const char *chunk = data + consumed;
        size_t remaining = data_len - consumed;

        switch (ctx->state) {
        case ESI_STATE_PASSTHROUGH: {
            const char *esi_pos = find_str(chunk, remaining, ESI_TAG_PREFIX);
            if (!esi_pos) {
                /* No complete tag prefix — check for a partial "<esi:"
                 * tail split at the chunk boundary and hold it back. */
                size_t k = partial_tag_tail(chunk, remaining);
                if (k > 0) {
                    /* Emit everything before the partial tail and retain
                     * the tail in ctx. Consumed is FULL: retained bytes are
                     * held internally, the caller must not re-send them. */
                    if (k < remaining &&
                        buf_append(ctx, chunk, remaining - k) != 0)
                        fprintf(stderr, "[esi] WARN: buffer limit hit, truncating\n");
                    hold_bytes(ctx, chunk + remaining - k, k);
                } else {
                    if (buf_append(ctx, chunk, remaining) != 0)
                        fprintf(stderr, "[esi] WARN: buffer limit hit (%zu bytes), truncating\n", ctx->buf_cap);
                }
                consumed = data_len;
            } else {
                size_t before = (size_t)(esi_pos - chunk);
                /* Any ESI tag must be terminated by '>'. If none follows the
                 * prefix in this chunk, the tag is split across chunks: hold
                 * from the prefix start (bounded; fail open past the cap).
                 * This precedes tag-name checks so a partial name like
                 * "<esi:inc" cannot fall into the literal unknown-tag path. */
                const char *gt = memchr(esi_pos, '>', remaining - before);
                if (!gt) {
                    size_t tail = remaining - before;
                    if (before > 0 && buf_append(ctx, chunk, before) != 0)
                        fprintf(stderr, "[esi] WARN: buffer limit hit, partial output\n");
                    if (tail <= ESI_HOLD_MAX) {
                        hold_bytes(ctx, esi_pos, tail);
                    } else {
                        buf_append(ctx, esi_pos, tail);
                    }
                    consumed = data_len;
                    break;
                }
                if (before > 0 && buf_append(ctx, chunk, before) != 0)
                    fprintf(stderr, "[esi] WARN: buffer limit hit, partial output\n");
                consumed += before;
                /* Determine tag type */
                const char *tag = esi_pos + ESI_TAG_PREFIX_LEN;
                size_t tag_rem = remaining - before - ESI_TAG_PREFIX_LEN;
                if (tag_rem >= 6 && strncasecmp(tag, "remove", 6) == 0) {
                    ctx->state = ESI_STATE_STRIP;
                    ctx->strip_depth = 1;
                    /* Skip past the opening tag (find '>') */
                    const char *gt = memchr(tag, '>', tag_rem);
                    if (gt)
                        consumed += ESI_TAG_PREFIX_LEN + (size_t)(gt - tag) + 1;
                    else
                        consumed = data_len; /* strip continues next chunk */
                } else if (tag_rem >= 7 && strncasecmp(tag, "comment", 7) == 0) {
                    ctx->state = ESI_STATE_STRIP;
                    ctx->strip_depth = 1;
                    const char *gt = memchr(tag, '>', tag_rem);
                    if (gt)
                        consumed += ESI_TAG_PREFIX_LEN + (size_t)(gt - tag) + 1;
                    else
                        consumed = data_len; /* strip continues next chunk */
                } else if (tag_rem >= 7 && strncasecmp(tag, "include", 7) == 0) {
                    /* esi:include — find closing >, extract src, read file */
                    const char *gt = memchr(tag, '>', tag_rem);
                    if (gt) {
                        size_t include_tag_len = ESI_TAG_PREFIX_LEN + (size_t)(gt - tag) + 1;
                        const char *tag_start = esi_pos; /* start of <esi: */
                        size_t full_tag_len = include_tag_len;

                        /* Extract src attribute */
                        char *src = esi_extract_src(tag_start, full_tag_len);
                        if (src) {
                            /* Static-first: try the file read bounded by the
                             * docroot (NULL docroot refuses, fail closed). */
                            size_t inc_len = 0;
                            char *file_part = src;
                            char *q = strchr(src, '?');
                            if (q) {
                                *q = '\0';
                                file_part = src;
                            }
                            char *inc_content = NULL;
                            if (!esi_src_is_handler(file_part))
                                inc_content = esi_include_read(
                                    file_part, ctx->docroot, ctx->docroot_len,
                                    &inc_len);
                            if (q)
                                *q = '?';

                            if (inc_content && inc_len > 0) {
                                buf_append(ctx, inc_content, inc_len);
                                free(inc_content);
                            } else if (!ctx->subrequest &&
                                       ctx->dyn_count < ESI_DYN_MAX &&
                                       esi_dyn_uri_valid(src)) {
                                /* Dynamic include: record the URI and emit a
                                 * placeholder; the hook holds output from now
                                 * on and splices the fetched body at EOF. */
                                ctx->dyn_uri[ctx->dyn_count] = strdup(src);
                                if (ctx->dyn_uri[ctx->dyn_count]) {
                                    emit_placeholder(ctx, ctx->dyn_count);
                                    ctx->dyn_count++;
                                    ctx->dyn_holding = 1;
                                }
                            }
                            /* else: subrequest (loop guard), cap exceeded, or
                             * invalid URI — tag silently removed (fail-open) */
                            free(src);
                            consumed += include_tag_len;
                        } else {
                            /* No src attribute — skip tag */
                            consumed += include_tag_len;
                        }
                    } else {
                        /* Unreachable: the universal incomplete-tag hold
                         * above guarantees a '>' exists in-tag. */
                        consumed = data_len;
                    }
                } else if (tag_rem >= 4 && strncasecmp(tag, "vars", 4) == 0) {
                    ctx->state = ESI_STATE_VARS;
                    const char *gt = memchr(tag, '>', tag_rem);
                    if (gt)
                        consumed += ESI_TAG_PREFIX_LEN + (size_t)(gt - tag) + 1;
                    else
                        consumed = data_len; /* vars continue next chunk */
                } else {
                    /* Unknown or include tag — passthrough in v1 */
                    buf_append(ctx, ESI_TAG_PREFIX, ESI_TAG_PREFIX_LEN);
                    consumed += ESI_TAG_PREFIX_LEN;
                }
            }
            break;
        }
        case ESI_STATE_STRIP: {
            /* Look for whichever closing tag appears first */
            const char *cr = find_str(chunk, remaining, ESI_REMOVE_CLOSE);
            const char *cc = find_str(chunk, remaining, ESI_COMMENT_CLOSE);
            const char *close = NULL;
            size_t clen = 0;
            if (cr && (!cc || cr < cc)) { close = cr; clen = 13; }
            else if (cc) { close = cc; clen = 14; }
            if (close) {
                consumed += (size_t)(close - chunk) + clen;
                ctx->state = ESI_STATE_PASSTHROUGH;
            } else {
                consumed = data_len; /* strip to next chunk */
            }
            break;
        }

        case ESI_STATE_VARS: {
            const char *close = find_str(chunk, remaining, ESI_VARS_CLOSE);
            if (!close) {
                /* Accumulate for variable substitution later */
                buf_append(ctx, chunk, remaining);
                consumed = data_len;
            } else {
                size_t before = (size_t)(close - chunk);
                buf_append(ctx, chunk, before);
                consumed += before + strlen(ESI_VARS_CLOSE);
                ctx->state = ESI_STATE_PASSTHROUGH;
            }
            break;
        }
        default:
            /* Shouldn't happen */
            buf_append(ctx, chunk, remaining);
            consumed = data_len;
            break;
        }
    }

    (void)out_start;
    *out_buf = ctx->buf;
    *out_len = ctx->buf_len;
    /* Hand-over clears the accumulator: the caller writes these bytes once,
     * and a later esi_finish must not re-deliver them. */
    ctx->buf_len = 0;
    if (work)
        free(work);
    /* Translate consumption over the combined buffer back to the caller's
     * chunk: the first prev_hold bytes were not part of this call. */
    if (consumed > prev_hold)
        consumed -= prev_hold;
    else
        consumed = 0;
    if (consumed > input_len)
        consumed = input_len;
    return consumed;
}

void esi_finish(esi_ctx_t *ctx, char **out_buf, size_t *out_len)
{
    if (!ctx || !out_buf || !out_len)
        return;
    /* A partial tag held at stream end never completed: emit it literally
     * (fail open — the client sees the markup rather than losing bytes). */
    if (ctx->hold_len > 0) {
        buf_append(ctx, ctx->hold, ctx->hold_len);
        ctx->hold_len = 0;
    }
    *out_buf = ctx->buf;
    *out_len = ctx->buf_len;
    /* Hand-over clears the accumulator so the same bytes can never be
     * emitted twice (each esi_process call resets it too). */
    ctx->buf_len = 0;
}

/* ------------------------------------------------------------------ */
/*  P8 v2: Variable substitution                                       */
/* ------------------------------------------------------------------ */

#include <strings.h>

/* Look up a variable name and return its value (NULL if unknown). */
static const char *lookup_var(const esi_vars_t *vars, const char *name, size_t name_len)
{
    if (!vars || !name)
        return NULL;
#define VAR_EQ(str) (name_len == sizeof(str)-1 && strncasecmp(name, str, name_len) == 0)
    if (VAR_EQ("HTTP_USER_AGENT"))  return vars->user_agent;
    if (VAR_EQ("HTTP_COOKIE"))      return vars->cookie;
    if (VAR_EQ("HTTP_REFERER"))     return vars->referer;
    if (VAR_EQ("HTTP_HOST"))        return vars->host;
    if (VAR_EQ("QUERY_STRING"))     return vars->query_string;
    if (VAR_EQ("REQUEST_METHOD"))   return vars->request_method;
    if (VAR_EQ("REMOTE_ADDR"))      return vars->remote_addr;
#undef VAR_EQ
    return NULL;
}

void esi_substitute_vars(const char *input, size_t input_len,
                         const esi_vars_t *vars,
                         char *out_buf, size_t out_buf_size, size_t *out_len)
{
    if (!input || !out_buf || !out_len) {
        if (out_len) *out_len = 0;
        return;
    }
    size_t opos = 0;
    size_t ipos = 0;

    while (ipos < input_len && opos < out_buf_size - 1) {
        if (input[ipos] == '$' && ipos + 1 < input_len && input[ipos+1] == '(') {
            /* Find closing paren */
            size_t close = ipos + 2;
            while (close < input_len && input[close] != ')')
                close++;
            if (close < input_len) {
                /* Extract variable name */
                const char *name = input + ipos + 2;
                size_t name_len = close - ipos - 2;
                const char *val = lookup_var(vars, name, name_len);
                if (val) {
                    size_t vlen = strlen(val);
                    size_t copy = (opos + vlen < out_buf_size - 1) ? vlen : (out_buf_size - 1 - opos);
                    memcpy(out_buf + opos, val, copy);
                    opos += copy;
                }
                /* Unknown variable: empty string (skip $(...) entirely) */
                ipos = close + 1;
            } else {
                /* No closing paren — treat as literal */
                out_buf[opos++] = input[ipos++];
            }
        } else {
            out_buf[opos++] = input[ipos++];
        }
    }
    out_buf[opos] = '\0';
    *out_len = opos;
}

int esi_should_process(const char *content_type, int content_type_len,
                       const char *content_encoding, int content_encoding_len)
{
    (void)content_encoding; (void)content_encoding_len; /* gzip handled at stream level */
    if (!content_type || content_type_len <= 0)
        return 0;
    /* text/html or application/xhtml+xml */
    if (content_type_len >= 9 && strncasecmp(content_type, "text/html", 9) == 0)
        return 1;
    if (content_type_len >= 21 && strncasecmp(content_type, "application/xhtml+xml", 21) == 0)
        return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Dynamic includes: URI validation + placeholder splice              */
/* ------------------------------------------------------------------ */

/* Handler-mapped suffixes must NEVER be served by the static fast path —
 * inlining their source would leak code instead of executing it. These go
 * through the loopback sub-request so the full pipeline (handlers, .htaccess
 * ACLs, modsec) applies, exactly like an SSI include virtual. */
static int esi_src_is_handler(const char *path)
{
    static const char *exts[] = {
        ".php", ".php3", ".php4", ".php5", ".php7", ".php8",
        ".phtml", ".phar", ".cgi", ".pl", ".py", ".rb", ".jsp",
        ".asp", ".aspx", NULL,
    };
    size_t n = strlen(path);
    for (int i = 0; exts[i]; i++) {
        size_t el = strlen(exts[i]);
        if (n >= el && strcasecmp(path + n - el, exts[i]) == 0)
            return 1;
    }
    return 0;
}

int esi_dyn_uri_valid(const char *uri)
{
    if (!uri || uri[0] != '/')
        return 0;
    if (uri[1] == '/') /* scheme-relative bypass */
        return 0;
    size_t n = 0;
    for (const char *p = uri; *p; p++, n++) {
        unsigned char c = (unsigned char)*p;
        if (n >= 2048)
            return 0;
        if (c <= 0x20 || c == 0x7f) /* no space/CR/LF/control: the URI goes
                                        verbatim into the request line */
            return 0;
    }
    return 1;
}

static int ph_index_at(const char *at)
{
    /* placeholder body bytes 4..7 hold the LE index; byte 7 is the 0x02
     * terminator — we store the index in bytes 4..6 only (24 bits) */
    return (int)((unsigned char)at[4] |
                 ((unsigned char)at[5] << 8) |
                 ((unsigned char)at[6] << 16));
}

void esi_dyn_splice(esi_ctx_t *ctx, int idx, const char *body, size_t body_len)
{
    if (!ctx || !ctx->dyn_hold || idx < 0 || idx >= ctx->dyn_count)
        return;
    if (!body)
        body_len = 0;

    /* One pass over dyn_hold copying everything except placeholder `idx`,
     * which is replaced by the fragment body. Bound the total to
     * ESI_MAX_BUFFER — on overflow the fragment is truncated (page still
     * completes; fail-open beats wedging). */
    size_t cap = ESI_MAX_BUFFER;
    char *out = malloc(ctx->dyn_hold_len + body_len + 1);
    if (!out)
        return; /* OOM: leave placeholder in place (it will be dropped by
                   the final strip pass) */
    size_t o = 0, i = 0;
    while (i + ESI_PH_LEN <= ctx->dyn_hold_len) {
        if (ctx->dyn_hold[i] == 0x01 && ctx->dyn_hold[i + 1] == 'L' &&
            ctx->dyn_hold[i + 2] == 'H' && ctx->dyn_hold[i + 3] == 'E' &&
            (unsigned char)ctx->dyn_hold[i + 7] == 0x02) {
            if (ph_index_at(ctx->dyn_hold + i) == idx) {
                if (body && body_len && o + body_len <= cap) {
                    memcpy(out + o, body, body_len);
                    o += body_len;
                }
                i += ESI_PH_LEN;
                continue;
            }
        }
        if (o < cap)
            out[o++] = ctx->dyn_hold[i];
        i++;
    }
    /* trailing bytes shorter than a placeholder */
    while (i < ctx->dyn_hold_len && o < cap)
        out[o++] = ctx->dyn_hold[i++];
    free(ctx->dyn_hold);
    ctx->dyn_hold = out;
    ctx->dyn_hold_len = o;
}

/* Strip any remaining placeholders (unresolved fragments) from the hold
 * buffer — fail-open final pass. */
void esi_dyn_strip_remaining(esi_ctx_t *ctx)
{
    if (!ctx || !ctx->dyn_hold)
        return;
    esi_dyn_splice(ctx, -2, NULL, 0); /* no idx matches -2: copies only */
    /* the copy pass above preserved other placeholders; remove them literally */
    size_t o = 0;
    for (size_t i = 0; i < ctx->dyn_hold_len; ) {
        if (i + ESI_PH_LEN <= ctx->dyn_hold_len &&
            ctx->dyn_hold[i] == 0x01 && ctx->dyn_hold[i + 1] == 'L' &&
            ctx->dyn_hold[i + 2] == 'H' && ctx->dyn_hold[i + 3] == 'E' &&
            (unsigned char)ctx->dyn_hold[i + 7] == 0x02) {
            i += ESI_PH_LEN; /* drop any placeholder regardless of index */
            continue;
        }
        ctx->dyn_hold[o++] = ctx->dyn_hold[i++];
    }
    ctx->dyn_hold_len = o;
}

/* ------------------------------------------------------------------ */
/*  ESI <esi:include> — static file include                            */
/* ------------------------------------------------------------------ */

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

/* Extract src="..." attribute value from an esi:include tag */
static char *esi_extract_src(const char *tag_start, size_t tag_len)
{
    /* Look for src=" or src=' */
    for (size_t i = 0; i + 5 < tag_len; i++) {
        if ((tag_start[i] == 's' || tag_start[i] == 'S') &&
            (tag_start[i+1] == 'r' || tag_start[i+1] == 'R') &&
            (tag_start[i+2] == 'c' || tag_start[i+2] == 'C') &&
            tag_start[i+3] == '=') {
            char quote = tag_start[i+4];
            if (quote == '"' || quote == '\'') {
                size_t start = i + 5;
                size_t end = start;
                while (end < tag_len && tag_start[end] != quote)
                    end++;
                if (end > start && end - start < 4096) {
                    char *src = malloc(end - start + 1);
                    if (src) {
                        memcpy(src, tag_start + start, end - start);
                        src[end - start] = '\0';
                        return src;
                    }
                }
            }
        }
    }
    return NULL;
}

char *esi_include_read(const char *src, const char *docroot, size_t docroot_len,
                       size_t *out_len)
{
    if (!src || !docroot || docroot_len == 0 || !out_len || src[0] != '/')
        return NULL;

    /* Security: reject path traversal */
    if (strstr(src, "..") != NULL)
        return NULL;

    /* Build full path */
    char path[4096];
    int n = snprintf(path, sizeof(path), "%.*s%s",
                     (int)docroot_len, docroot, src);
    if (n <= 0 || n >= (int)sizeof(path))
        return NULL;

    /* Containment check: resolve symlinks and require the result to stay
     * under the resolved docroot. O_NOFOLLOW below protects only the final
     * component; an intermediate symlinked directory (e.g. an
     * attacker-writable uploads/ pointing at /etc) would otherwise escape. */
    {
        char resolved_docroot[4096];
        if (!realpath(docroot, resolved_docroot))
            return NULL;
        char resolved_path[4096];
        if (!realpath(path, resolved_path))
            return NULL; /* missing file or permission — indistinguishable, refuse */
        size_t rd_len = strlen(resolved_docroot);
        /* Boundary-safe prefix: equal, or resolved_path starts with
         * resolved_docroot + '/'. */
        if (strncmp(resolved_path, resolved_docroot, rd_len) != 0)
            return NULL;
        if (resolved_path[rd_len] != '\0' && resolved_path[rd_len] != '/')
            return NULL;
        /* Re-target the open at the verified resolved path (the realpath
         * call above already followed intermediates; O_NOFOLLOW then pins
         * the final component). */
        n = snprintf(path, sizeof(path), "%s", resolved_path);
        if (n <= 0 || n >= (int)sizeof(path))
            return NULL;
    }

    /* Open with hardening (consistent with dirwalker.c) */
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return NULL;

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size > ESI_INCLUDE_MAX_SIZE) {
        close(fd);
        return NULL;
    }

    /* Read content */
    size_t size = (size_t)st.st_size;
    char *content = malloc(size + 1);
    if (!content) {
        close(fd);
        return NULL;
    }

    size_t total = 0;
    while (total < size) {
        ssize_t r = read(fd, content + total, size - total);
        if (r <= 0)
            break;
        total += (size_t)r;
    }
    close(fd);

    if (total == 0) {
        free(content);
        return NULL;
    }

    content[total] = '\0';
    *out_len = total;
    return content;
}
