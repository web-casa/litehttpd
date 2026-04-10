#ifndef HTACCESS_EXEC_FORCETYPE_H
#define HTACCESS_EXEC_FORCETYPE_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute ForceType directive — override Content-Type.
 */
int exec_force_type(lsi_session_t *session, const htaccess_directive_t *dir);

#ifdef __cplusplus
}
#endif

#endif /* HTACCESS_EXEC_FORCETYPE_H */
