/**
 * htaccess_exec_expires.c - Expires directive executor implementation
 *
 * Validates: Requirements 10.1, 10.2, 10.3
 */
#include "htaccess_exec_expires.h"
#include "htaccess_expires.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

int exec_expires(lsi_session_t *session, const htaccess_directive_t *directives,
                 const char *content_type)
{
    if (!session || !directives || !content_type)
        return LSI_ERROR;

    const htaccess_directive_t *dir;
    int active = 0;  /* Default: expires not active */

    /* First pass: find the last ExpiresActive directive to determine state */
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type == DIR_EXPIRES_ACTIVE) {
            active = dir->data.expires.active;
        }
    }

    /* If ExpiresActive is Off, do nothing */
    if (!active)
        return LSI_OK;

    /* Second pass: find matching ExpiresByType for the content_type */
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type != DIR_EXPIRES_BY_TYPE)
            continue;

        if (!dir->name)
            continue;

        /* Match MIME type (case-insensitive, prefix match).
         * Content-Type may include charset: "text/html; charset=UTF-8"
         * but ExpiresByType only specifies the base type: "text/html" */
        {
            size_t type_len = strlen(dir->name);
            if (strncasecmp(dir->name, content_type, type_len) != 0)
                continue;
            /* After the match, next char must be \0, ';', or ' ' */
            char next = content_type[type_len];
            if (next != '\0' && next != ';' && next != ' ')
                continue;
        }

        /* Determine duration in seconds */
        long duration_sec = dir->data.expires.duration_sec;
        if (duration_sec <= 0 && dir->value) {
            duration_sec = parse_expires_duration(dir->value);
        }
        if (duration_sec < 0)
            continue;

        /* Set Cache-Control: max-age=N */
        char cache_control_val[64];
        int cc_len = snprintf(cache_control_val, sizeof(cache_control_val),
                              "max-age=%ld", duration_sec);
        if (cc_len > 0 && (size_t)cc_len < sizeof(cache_control_val)) {
            lsi_session_set_resp_header(session,
                                        "Cache-Control", 13,
                                        cache_control_val, cc_len);
        }

        /* Set Expires header to an HTTP-date (current time + duration) */
        time_t expire_time = time(NULL) + duration_sec;
        struct tm tm_buf;
        char expires_val[64];
        if (gmtime_r(&expire_time, &tm_buf)) {
            int exp_len = (int)strftime(expires_val, sizeof(expires_val),
                                        "%a, %d %b %Y %H:%M:%S GMT", &tm_buf);
            if (exp_len > 0) {
                lsi_session_set_resp_header(session,
                                            "Expires", 7,
                                            expires_val, exp_len);
            }
        }

        /* First match wins */
        return LSI_OK;
    }

    /* Third pass: if no ExpiresByType matched, use ExpiresDefault as fallback */
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type != DIR_EXPIRES_DEFAULT)
            continue;

        long duration_sec = dir->data.expires.duration_sec;
        if (duration_sec <= 0 && dir->value) {
            duration_sec = parse_expires_duration(dir->value);
        }
        if (duration_sec < 0)
            continue;

        char cache_control_val[64];
        int cc_len = snprintf(cache_control_val, sizeof(cache_control_val),
                              "max-age=%ld", duration_sec);
        if (cc_len > 0 && (size_t)cc_len < sizeof(cache_control_val)) {
            lsi_session_set_resp_header(session,
                                        "Cache-Control", 13,
                                        cache_control_val, cc_len);
        }

        time_t expire_time = time(NULL) + duration_sec;
        struct tm tm_buf;
        char expires_val[64];
        if (gmtime_r(&expire_time, &tm_buf)) {
            int exp_len = (int)strftime(expires_val, sizeof(expires_val),
                                        "%a, %d %b %Y %H:%M:%S GMT", &tm_buf);
            if (exp_len > 0) {
                lsi_session_set_resp_header(session,
                                            "Expires", 7,
                                            expires_val, exp_len);
            }
        }

        return LSI_OK;
    }

    return LSI_OK;
}
