/**
 * htaccess_stream.h — stream-filter seam.
 *
 * OLS body filters must forward every downstream byte through
 * g_api->stream_write_next (consumed input is NOT auto-delivered — see
 * modgzip). This wrapper adds a test seam so unit tests can intercept
 * and record filter output without the real OLS API table.
 */
#ifndef HTACCESS_STREAM_H
#define HTACCESS_STREAM_H

#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Test override: when non-NULL, called instead of g_api->stream_write_next.
 * Returns the byte count accepted (as the OLS API does).
 */
extern int (*ht_stream_write_test_fn)(lsi_param_t *rec, const char *buf, int len);

/**
 * Forward filter output downstream. Returns bytes accepted, -1 on error,
 * or len when no transport exists (unit tests without an override —
 * treats the write as fully accepted).
 */
int ht_stream_write(lsi_param_t *rec, const char *buf, int len);

#ifdef __cplusplus
}
#endif

#endif /* HTACCESS_STREAM_H */
