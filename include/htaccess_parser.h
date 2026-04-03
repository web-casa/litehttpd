/**
 * htaccess_parser.h - .htaccess file parser for OLS .htaccess module
 *
 * Parses .htaccess file content into a linked list of htaccess_directive_t
 * nodes. Supports all 28 directive types including nested FilesMatch blocks.
 *
 * Validates: Requirements 2.1, 2.2, 2.3, 9.1
 */
#ifndef HTACCESS_PARSER_H
#define HTACCESS_PARSER_H

#include "htaccess_directive.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse .htaccess file content into a directive linked list.
 *
 * Processes the content line by line, creating a htaccess_directive_t node
 * for each recognised directive. Comments (lines starting with #) and
 * empty lines are skipped. On syntax error, a warning is logged with the
 * file path and line number, and the malformed line is skipped.
 *
 * FilesMatch blocks (<FilesMatch "pattern"> ... </FilesMatch>) are parsed
 * as a single DIR_FILES_MATCH directive whose children field holds the
 * nested directive list.
 *
 * The returned list preserves the original order of directives.
 *
 * @param content   .htaccess file content (not necessarily NUL-terminated).
 * @param len       Length of content in bytes.
 * @param filepath  File path for error messages (may be NULL).
 * @return Head of directive linked list, or NULL if no directives found.
 *         Caller must free with htaccess_directives_free().
 */
htaccess_directive_t *htaccess_parse(const char *content, size_t len,
                                     const char *filepath);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_PARSER_H */
