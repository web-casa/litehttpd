/**
 * htaccess_printer.h - .htaccess directive printer for OLS .htaccess module
 *
 * Formats a linked list of htaccess_directive_t nodes back into canonical
 * .htaccess text. Supports all 28 directive types including nested
 * FilesMatch blocks.
 *
 * Validates: Requirements 2.5
 */
#ifndef HTACCESS_PRINTER_H
#define HTACCESS_PRINTER_H

#include "htaccess_directive.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Format a directive linked list into .htaccess text.
 *
 * Each directive is printed on its own line in its canonical format.
 * FilesMatch blocks are printed with nested directives indented inside
 * <FilesMatch "pattern"> ... </FilesMatch> tags.
 *
 * The output is designed to be parseable by htaccess_parse() to support
 * the round-trip property (Property 1).
 *
 * @param head  Head of the directive linked list (may be NULL).
 * @return Dynamically allocated string (caller must free), or NULL on failure.
 */
char *htaccess_print(const htaccess_directive_t *head);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_PRINTER_H */
