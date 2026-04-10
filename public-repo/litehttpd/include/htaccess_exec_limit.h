/**
 * htaccess_exec_limit.h - Limit/LimitExcept directive executor
 *
 * Limit: execute children if request method IS in the method list.
 * LimitExcept: execute children if request method is NOT in the method list.
 *
 * Validates: Requirements 9.1-9.7
 */
#ifndef HTACCESS_EXEC_LIMIT_H
#define HTACCESS_EXEC_LIMIT_H

#include "htaccess_directive.h"
#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Check if a Limit/LimitExcept block's children should be executed.
 *
 * @param dir          A DIR_LIMIT or DIR_LIMIT_EXCEPT directive.
 * @param http_method  The request HTTP method (e.g., "GET", "POST").
 * @return 1 if children should be executed, 0 if not.
 */
int limit_should_exec(const htaccess_directive_t *dir, const char *http_method);

#ifdef __cplusplus
}
#endif

#endif /* HTACCESS_EXEC_LIMIT_H */
