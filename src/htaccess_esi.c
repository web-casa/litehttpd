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

void esi_ctx_init(esi_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = ESI_STATE_PASSTHROUGH;
    ctx->buf_cap = 16384;
    ctx->buf = malloc(ctx->buf_cap);
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

size_t esi_process(esi_ctx_t *ctx, const char *input, size_t input_len,
                   char **out_buf, size_t *out_len)
{
    if (!ctx || !input || !out_buf || !out_len)
        return 0;

    *out_len = 0;
    *out_buf = NULL;
    size_t consumed = 0;
    /* Use ctx->buf as output accumulation too (simpler than separate buffer) */
    size_t out_start = 0; /* offset in ctx->buf where output starts */
    ctx->buf_len = 0; /* clear for this chunk's output */

    while (consumed < input_len) {
        const char *chunk = input + consumed;
        size_t remaining = input_len - consumed;

        switch (ctx->state) {
        case ESI_STATE_PASSTHROUGH: {
            const char *esi_pos = find_str(chunk, remaining, ESI_TAG_PREFIX);
            if (!esi_pos) {
                /* No ESI tag in this chunk — pass through everything */
                if (buf_append(ctx, chunk, remaining) != 0)
                    fprintf(stderr, "[esi] WARN: buffer limit hit (%zu bytes), truncating\n", ctx->buf_cap);
                consumed = input_len;
            } else {
                size_t before = (size_t)(esi_pos - chunk);
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
                        consumed = input_len;
                } else if (tag_rem >= 7 && strncasecmp(tag, "comment", 7) == 0) {
                    ctx->state = ESI_STATE_STRIP;
                    ctx->strip_depth = 1;
                    const char *gt = memchr(tag, '>', tag_rem);
                    if (gt)
                        consumed += ESI_TAG_PREFIX_LEN + (size_t)(gt - tag) + 1;
                    else
                        consumed = input_len;
                } else if (tag_rem >= 4 && strncasecmp(tag, "vars", 4) == 0) {
                    ctx->state = ESI_STATE_VARS;
                    const char *gt = memchr(tag, '>', tag_rem);
                    if (gt)
                        consumed += ESI_TAG_PREFIX_LEN + (size_t)(gt - tag) + 1;
                    else
                        consumed = input_len;
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
                consumed = input_len;
            }
            break;
        }

        case ESI_STATE_VARS: {
            const char *close = find_str(chunk, remaining, ESI_VARS_CLOSE);
            if (!close) {
                /* Accumulate for variable substitution later */
                buf_append(ctx, chunk, remaining);
                consumed = input_len;
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
            consumed = input_len;
            break;
        }
    }

    (void)out_start;
    *out_buf = ctx->buf;
    *out_len = ctx->buf_len;
    return consumed;
}

void esi_finish(esi_ctx_t *ctx, char **out_buf, size_t *out_len)
{
    if (!ctx || !out_buf || !out_len)
        return;
    /* Any remaining buffer is the output */
    *out_buf = ctx->buf;
    *out_len = ctx->buf_len;
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
