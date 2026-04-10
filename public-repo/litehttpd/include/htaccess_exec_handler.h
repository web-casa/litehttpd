#ifndef HTACCESS_EXEC_HANDLER_H
#define HTACCESS_EXEC_HANDLER_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute AddHandler directive (DEBUG log only in OLS).
 */
int exec_add_handler(lsi_session_t *session, const htaccess_directive_t *dir);

/**
 * Execute SetHandler directive (DEBUG log only in OLS).
 */
int exec_set_handler(lsi_session_t *session, const htaccess_directive_t *dir);

/**
 * Execute AddType directive — set Content-Type for matching file extension.
 * @param filename  The request filename (basename) to match extensions against.
 */
int exec_add_type(lsi_session_t *session, const htaccess_directive_t *dir,
                  const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* HTACCESS_EXEC_HANDLER_H */
