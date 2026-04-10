#include "htaccess_exec_dirindex.h"
#include <string.h>
#include <stdlib.h>

int exec_directory_index(lsi_session_t *session,
                         const htaccess_directive_t *dir,
                         const char *target_dir)
{
    if (!session || !dir || !dir->value || !target_dir)
        return LSI_OK;

    /* Get the request URI to build the redirect target as a URI, not a path */
    int uri_len = 0;
    const char *uri = lsi_session_get_uri(session, &uri_len);
    if (!uri || uri_len <= 0)
        return LSI_OK;

    char *list = strdup(dir->value);
    if (!list)
        return LSI_ERROR;

    size_t dir_len = strlen(target_dir);
    char *saveptr = NULL;
    char *tok = strtok_r(list, " \t", &saveptr);

    while (tok) {
        /* Reject path traversal attempts */
        if (strstr(tok, "..")) {
            tok = strtok_r(NULL, " \t", &saveptr);
            continue;
        }
        /* Build full filesystem path: target_dir + "/" + filename */
        size_t tok_len = strlen(tok);
        size_t need_slash = (dir_len > 0 && target_dir[dir_len - 1] != '/') ? 1 : 0;
        char *path = (char *)malloc(dir_len + need_slash + tok_len + 1);
        if (!path) {
            free(list);
            return LSI_ERROR;
        }
        memcpy(path, target_dir, dir_len);
        if (need_slash)
            path[dir_len] = '/';
        memcpy(path + dir_len + need_slash, tok, tok_len + 1);

        if (lsi_session_file_exists(session, path)) {
            /* Build URI for internal redirect: uri + filename */
            size_t uri_need_slash = (uri_len > 0 && uri[uri_len - 1] != '/') ? 1 : 0;
            char *redirect_uri = (char *)malloc(uri_len + uri_need_slash + tok_len + 1);
            if (redirect_uri) {
                memcpy(redirect_uri, uri, uri_len);
                if (uri_need_slash)
                    redirect_uri[uri_len] = '/';
                memcpy(redirect_uri + uri_len + uri_need_slash, tok, tok_len + 1);
                lsi_session_set_uri_internal(session, redirect_uri,
                    (int)(uri_len + uri_need_slash + tok_len));
                free(redirect_uri);
            }
            free(path);
            free(list);
            return LSI_OK;
        }
        free(path);
        tok = strtok_r(NULL, " \t", &saveptr);
    }

    free(list);
    /* No file found — fall back to OLS default */
    return LSI_OK;
}
