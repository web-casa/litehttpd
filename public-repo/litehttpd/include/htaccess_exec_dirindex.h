#ifndef HTACCESS_EXEC_DIRINDEX_H
#define HTACCESS_EXEC_DIRINDEX_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute DirectoryIndex directive.
 * Checks each filename in the list for existence in target_dir,
 * sets internal redirect to the first existing file.
 *
 * @param target_dir  The directory path to check files in.
 * @return LSI_OK on success (or no match), LSI_ERROR on error.
 */
int exec_directory_index(lsi_session_t *session,
                         const htaccess_directive_t *dir,
                         const char *target_dir);

#ifdef __cplusplus
}
#endif

#endif /* HTACCESS_EXEC_DIRINDEX_H */
