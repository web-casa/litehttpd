#ifndef HTACCESS_ESI_H
#define HTACCESS_ESI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESI_MAX_BUFFER  (1024 * 1024)   /* 1 MB */

/* ── dynamic includes (v2) ──────────────────────────────────────────
 * An <esi:include> whose src does not resolve to a static file is fetched
 * via a loopback sub-request (full OLS pipeline: .htaccess, PHP, modsec).
 * Output is held from the first dynamic tag and joined at EOF under a
 * hard page-level deadline; every failure removes the tag (fail-open).
 * Loop protection: sub-requests carry X-LiteHttpd-Esi-Subrequest and the
 * module refuses dynamic fetches for marked requests. */
#define ESI_DYN_MAX           16
#define ESI_DYN_PER_MS_DFLT   1000
#define ESI_DYN_TOTAL_MS_DFLT 2500
#define ESI_DYN_BODY_MAX      (64 * 1024)
#define ESI_SUBREQ_MARKER     "X-LiteHttpd-Esi-Subrequest"

/* Test seam: the loopback fetcher is a function pointer so unit tests can
 * stub it. uri must be '/'-rooted and pre-validated. Returns a malloc'd
 * body (NULL on any failure/timeout) and sets *out_len via splice caller. */
typedef char *(*esi_dyn_fetch_fn_t)(const char *uri, const char *host_hdr,
                                    const char *cookie_hdr, int deadline_ms);
extern esi_dyn_fetch_fn_t esi_dyn_fetch_fn;
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
    /* Chunk-boundary hold-back for split tags (see ESI docs). */
    size_t       hold_len;
    char         hold[8192];
    /* Downstream backpressure: unwritten output parked at buf[0..pend_len].
     * Kept separate from buf_len, which esi_process clears per call. */
    size_t       pend_len;

    /* ── dynamic includes ── (names distinct from the chunk-reassembly
     * hold/hold_len above) */
    char        *dyn_uri[ESI_DYN_MAX]; /* owned URIs, indexed by placeholder */
    int          dyn_count;
    int          dyn_holding;           /* 1 once the first dynamic tag was seen */
    char        *dyn_hold;              /* held output (malloc'd, ESI_MAX_BUFFER) */
    size_t       dyn_hold_len, dyn_hold_cap;
    int          subrequest;            /* loop guard: this request IS a fragment */
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

/* Splice a fragment body (or "" on failure) over placeholder idx in the
 * dynamic hold buffer. Pure function on the ctx — unit-tested. */
void esi_dyn_splice(esi_ctx_t *ctx, int idx, const char *body, size_t body_len);

/* Validate a dynamic include URI for the loopback fetch: '/'-rooted, no
 * whitespace/CR/LF (header-injection guard), no scheme-relative '//'. */
int esi_dyn_uri_valid(const char *uri);

/* Final fail-open pass: drop any unresolved placeholders left in the
 * hold buffer after the join deadline. */
void esi_dyn_strip_remaining(esi_ctx_t *ctx);

/* ESI include configuration */
#define ESI_INCLUDE_MAX_SIZE   (64 * 1024)  /* 64KB max per include */
#define ESI_INCLUDE_MAX_DEPTH  3             /* max nesting depth */

/* Resolve an ESI include src to a filesystem path and read it.
 * Returns: malloc'd content (caller frees), or NULL on failure.
 * out_len receives content length. */
char *esi_include_read(const char *src, const char *docroot, size_t docroot_len,
                       size_t *out_len);
