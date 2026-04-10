/**
 * htaccess_exec_limit.c - Limit/LimitExcept directive executor
 *
 * Limit: children execute when request method IS in the method list.
 * LimitExcept: children execute when request method is NOT in the list.
 *
 * Validates: Requirements 9.1-9.7
 */
#include "htaccess_exec_limit.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>

/**
 * Check if http_method appears in a space-separated methods string.
 * Case-insensitive comparison.
 */
static int method_in_list(const char *methods, const char *http_method)
{
    if (!methods || !http_method)
        return 0;

    char *buf = strdup(methods);
    if (!buf)
        return 0;

    int found = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(buf, " \t", &saveptr);
    while (tok) {
        if (strcasecmp(tok, http_method) == 0) {
            found = 1;
            break;
        }
        tok = strtok_r(NULL, " \t", &saveptr);
    }

    free(buf);
    return found;
}

int limit_should_exec(const htaccess_directive_t *dir, const char *http_method)
{
    if (!dir || !http_method)
        return 0;

    int in_list = method_in_list(dir->data.limit.methods, http_method);

    if (dir->type == DIR_LIMIT)
        return in_list;       /* Limit: exec if method IS in list */
    else /* DIR_LIMIT_EXCEPT */
        return !in_list;      /* LimitExcept: exec if method NOT in list */
}
