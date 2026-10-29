#ifndef HTACCESS_ESI_H
#define HTACCESS_ESI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESI_MAX_BUFFER  (1024 * 1024)   /* 1 MB */
#define ESI_SCAN_WINDOW 4096             /* first-N-bytes bypass check */

typedef enum {
    ESI_STATE_PASSTHROUGH,     /* normal content, stream through */
    ESI_STATE_TAG_OPEN,        /* saw '<', accumulating potential '<esi:' */
    ESI_STATE_STRIP,           /* inside <esi:remove> or <esi:comment> */
    ESI_STATE_VARS,            /* inside <esi:vars> */
} esi_state_t;

typedef struct {
    esi_state_t  state;
    char        *buf;           /* accumulation buffer */
    size_t       buf_len;
    size_t       buf_cap;
    int          strip_depth;   /* nesting depth for remove/comment */
    int          bypassed;      /* 1 if no ESI detected in first window */
    /* Document root that <esi:include> resolves against. NULL/0 = includes
     * are REFUSED (fail closed): resolving against "/" would turn every
     * src into an absolute filesystem path, i.e. arbitrary file read. */
    const char  *docroot;
    size_t       docroot_len;
} esi_ctx_t;

/* Initialize/reset context. Call once per response.
 * docroot (may be NULL) bounds <esi:include> resolution. */
void esi_ctx_init(esi_ctx_t *ctx, const char *docroot, size_t docroot_len);
void esi_ctx_free(esi_ctx_t *ctx);

/**
 * Feed a body chunk into the ESI processor.
 * Returns: number of bytes consumed from input (caller advances ptr).
 * Output: transformed bytes are appended to *out_buf (caller-managed).
 *         Caller must stream_write_next(out_buf) after each call.
 */
size_t esi_process(esi_ctx_t *ctx, const char *input, size_t input_len,
                   char **out_buf, size_t *out_len);

/**
 * Final flush — process any remaining buffered content.
 * Call on RCVD_RESP_BODY.
 */
void esi_finish(esi_ctx_t *ctx, char **out_buf, size_t *out_len);

/**
 * Quick check: does this content contain ESI tags?
 * Returns 1 if "<esi:" found in first scan_len bytes.
 */
int esi_content_has_tags(const char *content, size_t len);

#ifdef __cplusplus
}
#endif
#endif

/* Variable substitution context (P8 v2) */
typedef struct {
    const char *user_agent;
    const char *cookie;
    const char *referer;
    const char *host;
    const char *query_string;
    const char *request_method;
    const char *remote_addr;
} esi_vars_t;

/**
 * Substitute $(VARIABLE) references in content using the provided values.
 * Unknown variables → empty string (W3C spec).
 * Returns substituted content in *out_buf (caller must not free, valid until next call).
 */
void esi_substitute_vars(const char *input, size_t input_len,
                         const esi_vars_t *vars,
                         char *out_buf, size_t out_buf_size, size_t *out_len);

/**
 * Check if a response should be ESI-processed based on headers.
 * Returns 1 if ESI should engage, 0 to bypass.
 */
int esi_should_process(const char *content_type, int content_type_len,
                       const char *content_encoding, int content_encoding_len);

/* ESI include configuration */
#define ESI_INCLUDE_MAX_SIZE   (64 * 1024)  /* 64KB max per include */
#define ESI_INCLUDE_MAX_DEPTH  3             /* max nesting depth */

/* Resolve an ESI include src to a filesystem path and read it.
 * Returns: malloc'd content (caller frees), or NULL on failure.
 * out_len receives content length. */
char *esi_include_read(const char *src, const char *docroot, size_t docroot_len,
                       size_t *out_len);
