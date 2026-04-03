#include "htaccess_exec_encoding.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>

/* Check if filename ends with the given extension (case-insensitive) */
static int has_ext(const char *filename, const char *ext)
{
    if (!filename || !ext)
        return 0;
    const char *e = ext;
    if (*e == '.') e++;
    size_t elen = strlen(e);
    size_t flen = strlen(filename);
    if (elen == 0 || flen <= elen)
        return 0;
    const char *dot = filename + flen - elen - 1;
    if (*dot != '.')
        return 0;
    return (strncasecmp(dot + 1, e, elen) == 0);
}

static int match_extensions(const char *filename, const char *ext_list)
{
    if (!ext_list) return 0;
    char *copy = strdup(ext_list);
    if (!copy) return 0;
    char *saveptr = NULL;
    char *tok = strtok_r(copy, " \t", &saveptr);
    while (tok) {
        if (has_ext(filename, tok)) {
            free(copy);
            return 1;
        }
        tok = strtok_r(NULL, " \t", &saveptr);
    }
    free(copy);
    return 0;
}

int exec_add_encoding(lsi_session_t *session, const htaccess_directive_t *dir,
                      const char *filename)
{
    if (!session || !dir || !dir->name || !filename)
        return LSI_OK;
    if (!match_extensions(filename, dir->value))
        return LSI_OK;

    lsi_session_set_resp_header(session,
                                "Content-Encoding", 16,
                                dir->name, (int)strlen(dir->name));
    return LSI_OK;
}

int exec_add_charset(lsi_session_t *session, const htaccess_directive_t *dir,
                     const char *filename)
{
    if (!session || !dir || !dir->name || !filename)
        return LSI_OK;
    if (!match_extensions(filename, dir->value))
        return LSI_OK;

    /* Get current Content-Type and append charset */
    int ct_len = 0;
    const char *ct = lsi_session_get_resp_content_type(session, &ct_len);

    char buf[512];
    if (ct && ct_len > 0) {
        /* Strip existing charset if present to avoid duplication */
        const char *semi = strstr(ct, "; charset=");
        if (!semi) semi = strstr(ct, ";charset=");
        int base_len = semi ? (int)(semi - ct) : ct_len;
        snprintf(buf, sizeof(buf), "%.*s; charset=%s", base_len, ct, dir->name);
    } else {
        snprintf(buf, sizeof(buf), "text/plain; charset=%s", dir->name);
    }
    lsi_session_set_resp_content_type(session, buf, (int)strlen(buf));
    return LSI_OK;
}
