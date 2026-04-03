#ifndef HTACCESS_EXEC_ENCODING_H
#define HTACCESS_EXEC_ENCODING_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute AddEncoding — set Content-Encoding for matching extension.
 */
int exec_add_encoding(lsi_session_t *session, const htaccess_directive_t *dir,
                      const char *filename);

/**
 * Execute AddCharset — append charset to Content-Type for matching extension.
 */
int exec_add_charset(lsi_session_t *session, const htaccess_directive_t *dir,
                     const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* HTACCESS_EXEC_ENCODING_H */
