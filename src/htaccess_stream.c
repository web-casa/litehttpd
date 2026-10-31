/**
 * htaccess_stream.c — stream-filter seam implementation.
 */
#include "htaccess_stream.h"

int (*ht_stream_write_test_fn)(lsi_param_t *rec, const char *buf, int len) = NULL;

int ht_stream_write(lsi_param_t *rec, const char *buf, int len)
{
    if (ht_stream_write_test_fn)
        return ht_stream_write_test_fn(rec, buf, len);
    if (g_api && g_api->stream_write_next)
        return g_api->stream_write_next(rec, buf, len);
    /* No transport (unit-test builds without an override): report the
     * write as fully accepted so filter logic still exercises its paths. */
    return len;
}
