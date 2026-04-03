/**
 * htaccess_exec_files_match.c - FilesMatch directive executor
 *
 * Compiles the FilesMatch regex pattern and matches it against the filename.
 * If matched, executes nested directives in original order by dispatching
 * to the appropriate executor (e.g., exec_header for Header directives).
 *
 * Validates: Requirements 9.1, 9.2, 9.3
 */
#include "htaccess_exec_files_match.h"
#include "htaccess_exec_header.h"
#include "htaccess_exec_error_doc.h"
#include "htaccess_exec_encoding.h"
#include "htaccess_exec_forcetype.h"
#include "htaccess_exec_handler.h"
#include "htaccess_expr.h"

#include <regex.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

/** Maximum regex pattern length to prevent ReDoS. */
#define MAX_REGEX_LEN 512

/* ------------------------------------------------------------------ */
/*  Thread-local regex cache for FilesMatch patterns                   */
/* ------------------------------------------------------------------ */
#define FM_REGEX_CACHE_SLOTS 4

static __thread struct {
    unsigned long hash;
    char         *pat;
    regex_t       re;
    int           valid;
} fm_cache[FM_REGEX_CACHE_SLOTS];

static unsigned long fm_djb2(const char *str)
{
    unsigned long h = 5381;
    while (*str)
        h = ((h << 5) + h) + (unsigned char)*str++;
    return h;
}

static regex_t *fm_get_cached_regex(const char *pattern)
{
    unsigned long h = fm_djb2(pattern);
    for (int i = 0; i < FM_REGEX_CACHE_SLOTS; i++) {
        if (fm_cache[i].valid && fm_cache[i].hash == h
            && strcmp(fm_cache[i].pat, pattern) == 0)
            return &fm_cache[i].re;
    }
    int slot = -1;
    for (int i = 0; i < FM_REGEX_CACHE_SLOTS; i++) {
        if (!fm_cache[i].valid) { slot = i; break; }
    }
    if (slot < 0) {
        regfree(&fm_cache[0].re);
        free(fm_cache[0].pat);
        for (int i = 0; i < FM_REGEX_CACHE_SLOTS - 1; i++)
            fm_cache[i] = fm_cache[i + 1];
        slot = FM_REGEX_CACHE_SLOTS - 1;
        fm_cache[slot].valid = 0;
    }
    if (regcomp(&fm_cache[slot].re, pattern, REG_EXTENDED | REG_NOSUB) != 0)
        return NULL;
    fm_cache[slot].hash = h;
    fm_cache[slot].pat = strdup(pattern);
    if (!fm_cache[slot].pat) {
        regfree(&fm_cache[slot].re);
        return NULL;
    }
    fm_cache[slot].valid = 1;
    return &fm_cache[slot].re;
}

/**
 * Dispatch and execute a single nested directive by type.
 *
 * @param session  LSIAPI session handle.
 * @param child    The nested directive to execute.
 * @return LSI_OK on success, LSI_ERROR on failure.
 */
static int dispatch_child(lsi_session_t *session,
                          const htaccess_directive_t *child,
                          const char *filename)
{
    switch (child->type) {
    /* All Header variants */
    case DIR_HEADER_SET:
    case DIR_HEADER_UNSET:
    case DIR_HEADER_APPEND:
    case DIR_HEADER_MERGE:
    case DIR_HEADER_ADD:
    case DIR_HEADER_ALWAYS_SET:
    case DIR_HEADER_ALWAYS_UNSET:
    case DIR_HEADER_ALWAYS_APPEND:
    case DIR_HEADER_ALWAYS_MERGE:
    case DIR_HEADER_ALWAYS_ADD:
    case DIR_HEADER_EDIT:
    case DIR_HEADER_EDIT_STAR:
    case DIR_HEADER_ALWAYS_EDIT:
    case DIR_HEADER_ALWAYS_EDIT_STAR:
        return exec_header(session, child);

    case DIR_ERROR_DOCUMENT:
        return exec_error_document(session, child);

    case DIR_FORCE_TYPE:
        return exec_force_type(session, child);

    case DIR_DEFAULT_TYPE:
        if (child->value) {
            int ct_len = 0;
            const char *ct = lsi_session_get_resp_content_type(session, &ct_len);
            if (!ct || ct_len == 0)
                lsi_session_set_resp_header(session,
                    "Content-Type", 12,
                    child->value, (int)strlen(child->value));
        }
        return LSI_OK;

    case DIR_ADD_TYPE:
        return exec_add_type(session, child, filename);

    case DIR_ADD_ENCODING:
        return exec_add_encoding(session, child, filename);

    case DIR_ADD_CHARSET:
        return exec_add_charset(session, child, filename);

    case DIR_ADD_DEFAULT_CHARSET:
        if (child->value && strcasecmp(child->value, "Off") != 0) {
            int ct_len = 0;
            const char *ct = lsi_session_get_resp_content_type(session, &ct_len);
            if (ct && ct_len > 5 && strncasecmp(ct, "text/", 5) == 0 &&
                !strcasestr(ct, "charset=")) {
                char buf[256];
                int blen = snprintf(buf, sizeof(buf), "%.*s; charset=%s",
                                    ct_len, ct, child->value);
                if (blen > 0 && blen < (int)sizeof(buf))
                    lsi_session_set_resp_content_type(session, buf, blen);
            }
        }
        return LSI_OK;

    case DIR_REQUEST_HEADER_SET:
    case DIR_REQUEST_HEADER_UNSET:
        /* RequestHeader in FilesMatch runs in response phase — too late
         * for the backend to see it. Log and skip. */
        lsi_log(session, LSI_LOG_WARN,
                "FilesMatch: RequestHeader at line %d ignored (response phase too late)",
                child->line_number);
        return LSI_OK;

    /* ACL directives — handled in request phase by container_acl_denies,
     * skip silently here (no warning needed) */
    case DIR_ORDER:
    case DIR_ALLOW_FROM:
    case DIR_DENY_FROM:
    case DIR_REQUIRE_ALL_DENIED:
    case DIR_REQUIRE_ALL_GRANTED:
        return LSI_OK;

    case DIR_IF: {
        /* Recursively evaluate nested If/ElseIf/Else chain */
        int branch_taken = 0;
        const htaccess_directive_t *d = child;
        while (d && (d->type == DIR_IF || d->type == DIR_ELSEIF || d->type == DIR_ELSE)) {
            if (!branch_taken) {
                int should_exec = 0;
                if (d->type == DIR_ELSE) {
                    should_exec = 1;
                } else {
                    expr_node_t *expr = (expr_node_t *)d->data.if_block.condition;
                    should_exec = (eval_expr(session, expr) > 0);
                }
                if (should_exec) {
                    branch_taken = 1;
                    const htaccess_directive_t *ic;
                    for (ic = d->data.if_block.children; ic; ic = ic->next)
                        dispatch_child(session, ic, filename);
                }
            }
            d = d->next;
        }
        return LSI_OK;
    }
    case DIR_ELSEIF:
    case DIR_ELSE:
        /* ElseIf/Else without preceding If — handled by chain above, skip */
        return LSI_OK;

    default:
        lsi_log(session, LSI_LOG_WARN,
                "FilesMatch: unsupported nested directive type %d at line %d",
                (int)child->type, child->line_number);
        return LSI_OK;
    }
}

int exec_files_match(lsi_session_t *session, const htaccess_directive_t *dir,
                     const char *filename)
{
    int rc;
    const htaccess_directive_t *child;
    const char *pattern;

    if (!session || !dir || !filename)
        return LSI_ERROR;

    if (dir->type != DIR_FILES_MATCH)
        return LSI_ERROR;

    pattern = dir->data.files_match.pattern;
    if (!pattern)
        return LSI_ERROR;

    /* Reject excessively long patterns to mitigate ReDoS */
    if (strlen(pattern) > MAX_REGEX_LEN) {
        lsi_log(session, LSI_LOG_WARN,
                "FilesMatch: regex pattern too long (%zu > %d) at line %d",
                strlen(pattern), MAX_REGEX_LEN, dir->line_number);
        return LSI_ERROR;
    }

    /* Compile the regex pattern (cached) */
    regex_t *rep = fm_get_cached_regex(pattern);
    if (!rep) {
        lsi_log(session, LSI_LOG_WARN,
                "FilesMatch: invalid regex pattern '%s' at line %d",
                pattern, dir->line_number);
        return LSI_ERROR;
    }

    /* Match filename against the pattern */
    rc = regexec(rep, filename, 0, NULL, 0);

    if (rc != 0) {
        /* No match — skip all children */
        return 0; /* 0 = no match (not error) */
    }

    /* Match found — execute nested directives in order */
    child = dir->data.files_match.children;
    while (child) {
        if (child->type == DIR_IF) {
            /* Evaluate If/ElseIf/Else chain as a unit */
            int branch_taken = 0;
            const htaccess_directive_t *d = child;
            while (d && (d->type == DIR_IF || d->type == DIR_ELSEIF || d->type == DIR_ELSE)) {
                if (!branch_taken) {
                    int should_exec = 0;
                    if (d->type == DIR_ELSE) {
                        should_exec = 1;
                    } else {
                        expr_node_t *expr = (expr_node_t *)d->data.if_block.condition;
                        should_exec = (eval_expr(session, expr) > 0);
                    }
                    if (should_exec) {
                        branch_taken = 1;
                        const htaccess_directive_t *ic;
                        for (ic = d->data.if_block.children; ic; ic = ic->next)
                            dispatch_child(session, ic, filename);
                    }
                }
                d = d->next;
            }
            child = d; /* Skip past consumed chain */
        } else {
            dispatch_child(session, child, filename);
            child = child->next;
        }
    }

    return 1; /* 1 = matched and executed */
}

int fm_regex_matches(const char *pattern, const char *filename)
{
    if (!pattern || !filename)
        return -1;
    if (strlen(pattern) > MAX_REGEX_LEN)
        return -1;
    regex_t *rep = fm_get_cached_regex(pattern);
    if (!rep)
        return -1;
    return (regexec(rep, filename, 0, NULL, 0) == 0) ? 1 : 0;
}
