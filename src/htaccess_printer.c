/**
 * htaccess_printer.c - .htaccess directive printer implementation
 *
 * Formats a linked list of htaccess_directive_t nodes into canonical
 * .htaccess text. Output is designed to round-trip through htaccess_parse().
 *
 * Validates: Requirements 2.5
 */
#include "htaccess_printer.h"
#include "htaccess_expr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Dynamic string buffer                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} strbuf_t;

static int strbuf_init(strbuf_t *sb, size_t initial_cap)
{
    sb->buf = (char *)malloc(initial_cap);
    if (!sb->buf)
        return -1;
    sb->buf[0] = '\0';
    sb->len = 0;
    sb->cap = initial_cap;
    return 0;
}

static int strbuf_ensure(strbuf_t *sb, size_t extra)
{
    size_t needed = sb->len + extra + 1;
    if (needed <= sb->cap)
        return 0;
    size_t new_cap = sb->cap * 2;
    if (new_cap < needed)
        new_cap = needed;
    char *tmp = (char *)realloc(sb->buf, new_cap);
    if (!tmp)
        return -1;
    sb->buf = tmp;
    sb->cap = new_cap;
    return 0;
}

static int strbuf_append(strbuf_t *sb, const char *s)
{
    size_t slen = strlen(s);
    if (strbuf_ensure(sb, slen) != 0)
        return -1;
    memcpy(sb->buf + sb->len, s, slen);
    sb->len += slen;
    sb->buf[sb->len] = '\0';
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Per-directive printers                                             */
/* ------------------------------------------------------------------ */

/**
 * Append " env=VAR" suffix if the directive has an env condition.
 */
static int print_env_suffix(strbuf_t *sb, const htaccess_directive_t *d)
{
    if (d->env_condition) {
        if (strbuf_append(sb, " env=") != 0) return -1;
        if (strbuf_append(sb, d->env_condition) != 0) return -1;
    }
    return 0;
}

/**
 * Print a single directive into the string buffer.
 * Returns 0 on success, -1 on allocation failure.
 */
static int print_directive(strbuf_t *sb, const htaccess_directive_t *d)
{
    char tmp[64];

    switch (d->type) {

    /* --- Header directives --- */
    case DIR_HEADER_SET:
        if (strbuf_append(sb, "Header set ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (strbuf_append(sb, " ") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        if (print_env_suffix(sb, d) != 0) return -1;
        break;

    case DIR_HEADER_UNSET:
        if (strbuf_append(sb, "Header unset ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (print_env_suffix(sb, d) != 0) return -1;
        break;

    case DIR_HEADER_APPEND:
        if (strbuf_append(sb, "Header append ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (strbuf_append(sb, " ") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        if (print_env_suffix(sb, d) != 0) return -1;
        break;

    case DIR_HEADER_MERGE:
        if (strbuf_append(sb, "Header merge ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (strbuf_append(sb, " ") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        if (print_env_suffix(sb, d) != 0) return -1;
        break;

    case DIR_HEADER_ADD:
        if (strbuf_append(sb, "Header add ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (strbuf_append(sb, " ") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        if (print_env_suffix(sb, d) != 0) return -1;
        break;

    /* --- RequestHeader directives --- */
    case DIR_REQUEST_HEADER_SET:
        if (strbuf_append(sb, "RequestHeader set ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (d->value) {
            int q = (strchr(d->value, ' ') != NULL);
            if (strbuf_append(sb, " ") != 0) return -1;
            if (q && strbuf_append(sb, "\"") != 0) return -1;
            if (strbuf_append(sb, d->value) != 0) return -1;
            if (q && strbuf_append(sb, "\"") != 0) return -1;
        }
        break;

    case DIR_REQUEST_HEADER_UNSET:
        if (strbuf_append(sb, "RequestHeader unset ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        break;

    /* --- PHP directives --- */
    case DIR_PHP_VALUE:
        if (strbuf_append(sb, "php_value ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (strbuf_append(sb, " ") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        break;

    case DIR_PHP_FLAG:
        if (strbuf_append(sb, "php_flag ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (strbuf_append(sb, " ") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        break;

    case DIR_PHP_ADMIN_VALUE:
        if (strbuf_append(sb, "php_admin_value ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (strbuf_append(sb, " ") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        break;

    case DIR_PHP_ADMIN_FLAG:
        if (strbuf_append(sb, "php_admin_flag ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (strbuf_append(sb, " ") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        break;

    /* --- Access control directives --- */
    case DIR_ORDER:
        if (d->data.acl.order == ORDER_ALLOW_DENY) {
            if (strbuf_append(sb, "Order Allow,Deny") != 0) return -1;
        } else {
            if (strbuf_append(sb, "Order Deny,Allow") != 0) return -1;
        }
        break;

    case DIR_ALLOW_FROM:
        if (strbuf_append(sb, "Allow from ") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        break;

    case DIR_DENY_FROM:
        if (strbuf_append(sb, "Deny from ") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        break;

    /* --- Redirect directives --- */
    case DIR_REDIRECT:
        if (strbuf_append(sb, "Redirect") != 0) return -1;
        if (d->data.redirect.status_code != 302) {
            const char *kw = NULL;
            switch (d->data.redirect.status_code) {
            case 301: kw = "permanent"; break;
            case 303: kw = "seeother";  break;
            case 410: kw = "gone";       break;
            }
            if (kw) {
                if (strbuf_append(sb, " ") != 0) return -1;
                if (strbuf_append(sb, kw) != 0) return -1;
            } else {
                snprintf(tmp, sizeof(tmp), " %d", d->data.redirect.status_code);
                if (strbuf_append(sb, tmp) != 0) return -1;
            }
        }
        if (strbuf_append(sb, " ") != 0) return -1;
        if (d->name) {
            if (strbuf_append(sb, d->name) != 0) return -1;
        }
        if (d->value) {
            if (strbuf_append(sb, " ") != 0) return -1;
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    case DIR_REDIRECT_MATCH:
        if (strbuf_append(sb, "RedirectMatch") != 0) return -1;
        if (d->data.redirect.status_code != 302) {
            const char *kw = NULL;
            switch (d->data.redirect.status_code) {
            case 301: kw = "permanent"; break;
            case 303: kw = "seeother";  break;
            case 410: kw = "gone";       break;
            }
            if (kw) {
                if (strbuf_append(sb, " ") != 0) return -1;
                if (strbuf_append(sb, kw) != 0) return -1;
            } else {
                snprintf(tmp, sizeof(tmp), " %d", d->data.redirect.status_code);
                if (strbuf_append(sb, tmp) != 0) return -1;
            }
        }
        if (strbuf_append(sb, " ") != 0) return -1;
        if (d->data.redirect.pattern) {
            int q = (strchr(d->data.redirect.pattern, ' ') != NULL);
            if (q && strbuf_append(sb, "\"") != 0) return -1;
            if (strbuf_append(sb, d->data.redirect.pattern) != 0) return -1;
            if (q && strbuf_append(sb, "\"") != 0) return -1;
        }
        if (d->value) {
            int q = (strchr(d->value, ' ') != NULL);
            if (strbuf_append(sb, " ") != 0) return -1;
            if (q && strbuf_append(sb, "\"") != 0) return -1;
            if (strbuf_append(sb, d->value) != 0) return -1;
            if (q && strbuf_append(sb, "\"") != 0) return -1;
        }
        break;

    /* --- ErrorDocument --- */
    case DIR_ERROR_DOCUMENT:
        snprintf(tmp, sizeof(tmp), "ErrorDocument %d ", d->data.error_doc.error_code);
        if (strbuf_append(sb, tmp) != 0) return -1;
        if (d->value && d->value[0] == '"') {
            /* Value already contains quotes (text message mode) — output directly */
            if (strbuf_append(sb, d->value) != 0) return -1;
        } else {
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    /* --- FilesMatch block --- */
    case DIR_FILES_MATCH:
        if (strbuf_append(sb, "<FilesMatch \"") != 0) return -1;
        if (d->data.files_match.pattern) {
            if (strbuf_append(sb, d->data.files_match.pattern) != 0) return -1;
        }
        if (strbuf_append(sb, "\">\n") != 0) return -1;
        /* Print nested children */
        for (const htaccess_directive_t *child = d->data.files_match.children;
             child; child = child->next) {
            if (print_directive(sb, child) != 0) return -1;
            if (strbuf_append(sb, "\n") != 0) return -1;
        }
        if (strbuf_append(sb, "</FilesMatch>") != 0) return -1;
        break;

    /* --- IfModule block --- */
    case DIR_IFMODULE:
        if (strbuf_append(sb, "<IfModule ") != 0) return -1;
        if (d->name) {
            /* Quote if name contains spaces */
            int needs_quote = (strchr(d->name, ' ') != NULL);
            if (needs_quote && strbuf_append(sb, "\"") != 0) return -1;
            if (strbuf_append(sb, d->name) != 0) return -1;
            if (needs_quote && strbuf_append(sb, "\"") != 0) return -1;
        }
        if (strbuf_append(sb, ">\n") != 0) return -1;
        /* Print nested children */
        for (const htaccess_directive_t *child = d->data.ifmodule.children;
             child; child = child->next) {
            if (print_directive(sb, child) != 0) return -1;
            if (strbuf_append(sb, "\n") != 0) return -1;
        }
        if (strbuf_append(sb, "</IfModule>") != 0) return -1;
        break;

    /* --- Header always directives --- */
    case DIR_HEADER_ALWAYS_SET:
        if (strbuf_append(sb, "Header always set ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (strbuf_append(sb, " ") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        if (print_env_suffix(sb, d) != 0) return -1;
        break;

    case DIR_HEADER_ALWAYS_UNSET:
        if (strbuf_append(sb, "Header always unset ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (print_env_suffix(sb, d) != 0) return -1;
        break;

    case DIR_HEADER_ALWAYS_APPEND:
        if (strbuf_append(sb, "Header always append ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (strbuf_append(sb, " ") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        if (print_env_suffix(sb, d) != 0) return -1;
        break;

    case DIR_HEADER_ALWAYS_MERGE:
        if (strbuf_append(sb, "Header always merge ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (strbuf_append(sb, " ") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        if (print_env_suffix(sb, d) != 0) return -1;
        break;

    case DIR_HEADER_ALWAYS_ADD:
        if (strbuf_append(sb, "Header always add ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (strbuf_append(sb, " ") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        if (print_env_suffix(sb, d) != 0) return -1;
        break;

    /* --- Header edit directives --- */
    case DIR_HEADER_EDIT:
    case DIR_HEADER_EDIT_STAR:
    case DIR_HEADER_ALWAYS_EDIT:
    case DIR_HEADER_ALWAYS_EDIT_STAR: {
        const char *prefix = "Header ";
        if (d->type == DIR_HEADER_ALWAYS_EDIT || d->type == DIR_HEADER_ALWAYS_EDIT_STAR)
            prefix = "Header always ";
        if (strbuf_append(sb, prefix) != 0) return -1;
        if (d->type == DIR_HEADER_EDIT_STAR || d->type == DIR_HEADER_ALWAYS_EDIT_STAR) {
            if (strbuf_append(sb, "edit* ") != 0) return -1;
        } else {
            if (strbuf_append(sb, "edit ") != 0) return -1;
        }
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (d->data.header_ext.edit_pattern) {
            int q = (strchr(d->data.header_ext.edit_pattern, ' ') != NULL);
            if (strbuf_append(sb, " ") != 0) return -1;
            if (q && strbuf_append(sb, "\"") != 0) return -1;
            if (strbuf_append(sb, d->data.header_ext.edit_pattern) != 0) return -1;
            if (q && strbuf_append(sb, "\"") != 0) return -1;
        }
        if (d->value) {
            int q = (strchr(d->value, ' ') != NULL);
            if (strbuf_append(sb, " ") != 0) return -1;
            if (q && strbuf_append(sb, "\"") != 0) return -1;
            if (strbuf_append(sb, d->value) != 0) return -1;
            if (q && strbuf_append(sb, "\"") != 0) return -1;
        }
        if (print_env_suffix(sb, d) != 0) return -1;
        break;
    }

    /* --- Options directive --- */
    case DIR_OPTIONS:
        if (strbuf_append(sb, "Options ") != 0) return -1;
        if (d->value) {
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    /* --- Files block --- */
    case DIR_FILES:
        if (strbuf_append(sb, "<Files ") != 0) return -1;
        if (d->name) {
            int needs_quote = (strchr(d->name, ' ') != NULL);
            if (needs_quote && strbuf_append(sb, "\"") != 0) return -1;
            if (strbuf_append(sb, d->name) != 0) return -1;
            if (needs_quote && strbuf_append(sb, "\"") != 0) return -1;
        }
        if (strbuf_append(sb, ">\n") != 0) return -1;
        /* Print nested children */
        for (const htaccess_directive_t *child = d->data.files.children;
             child; child = child->next) {
            if (print_directive(sb, child) != 0) return -1;
            if (strbuf_append(sb, "\n") != 0) return -1;
        }
        if (strbuf_append(sb, "</Files>") != 0) return -1;
        break;

    /* --- Expires directives --- */
    case DIR_EXPIRES_ACTIVE:
        if (strbuf_append(sb, "ExpiresActive ") != 0) return -1;
        if (strbuf_append(sb, d->data.expires.active ? "On" : "Off") != 0) return -1;
        break;

    case DIR_EXPIRES_BY_TYPE:
        if (strbuf_append(sb, "ExpiresByType ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (strbuf_append(sb, " \"") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        if (strbuf_append(sb, "\"") != 0) return -1;
        break;

    case DIR_EXPIRES_DEFAULT:
        if (strbuf_append(sb, "ExpiresDefault \"") != 0) return -1;
        if (strbuf_append(sb, d->value) != 0) return -1;
        if (strbuf_append(sb, "\"") != 0) return -1;
        break;

    /* --- Require directives --- */
    case DIR_REQUIRE_ALL_GRANTED:
        if (strbuf_append(sb, "Require all granted") != 0) return -1;
        break;

    case DIR_REQUIRE_ALL_DENIED:
        if (strbuf_append(sb, "Require all denied") != 0) return -1;
        break;

    case DIR_REQUIRE_IP:
        if (strbuf_append(sb, "Require ip ") != 0) return -1;
        if (d->value) {
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    case DIR_REQUIRE_NOT_IP:
        if (strbuf_append(sb, "Require not ip ") != 0) return -1;
        if (d->value) {
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    case DIR_REQUIRE_VALID_USER:
        if (strbuf_append(sb, "Require valid-user") != 0) return -1;
        break;

    case DIR_REQUIRE_ENV:
        if (strbuf_append(sb, "Require env ") != 0) return -1;
        if (d->name) {
            if (strbuf_append(sb, d->name) != 0) return -1;
        }
        break;

    /* --- Auth directives --- */
    case DIR_AUTH_TYPE:
        if (strbuf_append(sb, "AuthType ") != 0) return -1;
        if (d->value) {
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    case DIR_AUTH_NAME:
        if (strbuf_append(sb, "AuthName \"") != 0) return -1;
        if (d->value) {
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        if (strbuf_append(sb, "\"") != 0) return -1;
        break;

    case DIR_AUTH_USER_FILE:
        if (strbuf_append(sb, "AuthUserFile ") != 0) return -1;
        if (d->value) {
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    case DIR_REQUIRE_ANY_OPEN:
        if (strbuf_append(sb, "<RequireAny>\n") != 0) return -1;
        for (const htaccess_directive_t *child = d->data.require_container.children;
             child; child = child->next) {
            if (print_directive(sb, child) != 0) return -1;
            if (strbuf_append(sb, "\n") != 0) return -1;
        }
        if (strbuf_append(sb, "</RequireAny>") != 0) return -1;
        break;

    case DIR_REQUIRE_ALL_OPEN:
        if (strbuf_append(sb, "<RequireAll>\n") != 0) return -1;
        for (const htaccess_directive_t *child = d->data.require_container.children;
             child; child = child->next) {
            if (print_directive(sb, child) != 0) return -1;
            if (strbuf_append(sb, "\n") != 0) return -1;
        }
        if (strbuf_append(sb, "</RequireAll>") != 0) return -1;
        break;

    /* --- Limit / LimitExcept blocks --- */
    case DIR_LIMIT:
        if (strbuf_append(sb, "<Limit ") != 0) return -1;
        if (d->data.limit.methods) {
            if (strbuf_append(sb, d->data.limit.methods) != 0) return -1;
        }
        if (strbuf_append(sb, ">\n") != 0) return -1;
        for (const htaccess_directive_t *child = d->data.limit.children;
             child; child = child->next) {
            if (print_directive(sb, child) != 0) return -1;
            if (strbuf_append(sb, "\n") != 0) return -1;
        }
        if (strbuf_append(sb, "</Limit>") != 0) return -1;
        break;

    case DIR_LIMIT_EXCEPT:
        if (strbuf_append(sb, "<LimitExcept ") != 0) return -1;
        if (d->data.limit.methods) {
            if (strbuf_append(sb, d->data.limit.methods) != 0) return -1;
        }
        if (strbuf_append(sb, ">\n") != 0) return -1;
        for (const htaccess_directive_t *child = d->data.limit.children;
             child; child = child->next) {
            if (print_directive(sb, child) != 0) return -1;
            if (strbuf_append(sb, "\n") != 0) return -1;
        }
        if (strbuf_append(sb, "</LimitExcept>") != 0) return -1;
        break;

    /* --- Environment variable directives --- */
    case DIR_SETENV:
        if (strbuf_append(sb, "SetEnv ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (d->value) {
            int q = (strchr(d->value, ' ') != NULL);
            if (strbuf_append(sb, " ") != 0) return -1;
            if (q && strbuf_append(sb, "\"") != 0) return -1;
            if (strbuf_append(sb, d->value) != 0) return -1;
            if (q && strbuf_append(sb, "\"") != 0) return -1;
        }
        break;

    case DIR_SETENVIF:
    case DIR_SETENVIF_NOCASE:
        if (strbuf_append(sb, d->type == DIR_SETENVIF_NOCASE
                              ? "SetEnvIfNoCase " : "SetEnvIf ") != 0) return -1;
        if (d->data.envif.attribute) {
            if (strbuf_append(sb, d->data.envif.attribute) != 0) return -1;
        }
        if (strbuf_append(sb, " ") != 0) return -1;
        if (d->data.envif.pattern) {
            int q = (strchr(d->data.envif.pattern, ' ') != NULL);
            if (q && strbuf_append(sb, "\"") != 0) return -1;
            if (strbuf_append(sb, d->data.envif.pattern) != 0) return -1;
            if (q && strbuf_append(sb, "\"") != 0) return -1;
        }
        if (strbuf_append(sb, " ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        /* Only output =value if value is non-empty (preserve bare-name syntax) */
        if (d->value && d->value[0] != '\0') {
            if (strbuf_append(sb, "=") != 0) return -1;
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    case DIR_BROWSER_MATCH:
        if (strbuf_append(sb, "BrowserMatch ") != 0) return -1;
        if (d->data.envif.pattern) {
            int q = (strchr(d->data.envif.pattern, ' ') != NULL);
            if (q && strbuf_append(sb, "\"") != 0) return -1;
            if (strbuf_append(sb, d->data.envif.pattern) != 0) return -1;
            if (q && strbuf_append(sb, "\"") != 0) return -1;
        }
        if (strbuf_append(sb, " ") != 0) return -1;
        if (strbuf_append(sb, d->name) != 0) return -1;
        if (d->value && d->value[0] != '\0') {
            if (strbuf_append(sb, "=") != 0) return -1;
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    /* --- Brute force protection directives --- */
    case DIR_BRUTE_FORCE_PROTECTION:
        if (strbuf_append(sb, "BruteForceProtection ") != 0) return -1;
        if (strbuf_append(sb, d->data.brute_force.enabled ? "On" : "Off") != 0) return -1;
        break;

    case DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS:
        snprintf(tmp, sizeof(tmp), "BruteForceAllowedAttempts %d",
                 d->data.brute_force.allowed_attempts);
        if (strbuf_append(sb, tmp) != 0) return -1;
        break;

    case DIR_BRUTE_FORCE_WINDOW:
        snprintf(tmp, sizeof(tmp), "BruteForceWindow %d",
                 d->data.brute_force.window_sec);
        if (strbuf_append(sb, tmp) != 0) return -1;
        break;

    case DIR_BRUTE_FORCE_ACTION: {
        const char *action_str = "block";
        if (d->data.brute_force.action == BF_ACTION_THROTTLE)
            action_str = "throttle";
        else if (d->data.brute_force.action == BF_ACTION_LOG)
            action_str = "log";
        if (strbuf_append(sb, "BruteForceAction ") != 0) return -1;
        if (strbuf_append(sb, action_str) != 0) return -1;
        break;
    }

    case DIR_BRUTE_FORCE_THROTTLE_DURATION:
        snprintf(tmp, sizeof(tmp), "BruteForceThrottleDuration %d",
                 d->data.brute_force.throttle_ms);
        if (strbuf_append(sb, tmp) != 0) return -1;
        break;

    case DIR_BRUTE_FORCE_X_FORWARDED_FOR:
        if (strbuf_append(sb, "BruteForceXForwardedFor ") != 0) return -1;
        if (strbuf_append(sb, d->data.brute_force.enabled ? "On" : "Off") != 0) return -1;
        break;

    case DIR_BRUTE_FORCE_WHITELIST:
        if (strbuf_append(sb, "BruteForceWhitelist ") != 0) return -1;
        if (d->value) {
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    case DIR_BRUTE_FORCE_PROTECT_PATH:
        if (strbuf_append(sb, "BruteForceProtectPath ") != 0) return -1;
        if (d->value) {
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    /* --- Handler/Type directives --- */
    case DIR_ADD_HANDLER:
        if (strbuf_append(sb, "AddHandler ") != 0) return -1;
        if (d->name) {
            if (strbuf_append(sb, d->name) != 0) return -1;
        }
        if (d->value) {
            if (strbuf_append(sb, " ") != 0) return -1;
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    case DIR_SET_HANDLER:
        if (strbuf_append(sb, "SetHandler ") != 0) return -1;
        if (d->value) {
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    case DIR_ADD_TYPE:
        if (strbuf_append(sb, "AddType ") != 0) return -1;
        if (d->name) {
            if (strbuf_append(sb, d->name) != 0) return -1;
        }
        if (d->value) {
            if (strbuf_append(sb, " ") != 0) return -1;
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    case DIR_DIRECTORY_INDEX:
        if (strbuf_append(sb, "DirectoryIndex ") != 0) return -1;
        if (d->value) {
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    case DIR_FORCE_TYPE:
        if (strbuf_append(sb, "ForceType ") != 0) return -1;
        if (d->value) {
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    case DIR_ADD_ENCODING:
        if (strbuf_append(sb, "AddEncoding ") != 0) return -1;
        if (d->name) {
            if (strbuf_append(sb, d->name) != 0) return -1;
        }
        if (d->value) {
            if (strbuf_append(sb, " ") != 0) return -1;
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    case DIR_ADD_CHARSET:
        if (strbuf_append(sb, "AddCharset ") != 0) return -1;
        if (d->name) {
            if (strbuf_append(sb, d->name) != 0) return -1;
        }
        if (d->value) {
            if (strbuf_append(sb, " ") != 0) return -1;
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    /* --- Rewrite directives --- */
    case DIR_REWRITE_ENGINE:
        if (strbuf_append(sb, "RewriteEngine ") != 0) return -1;
        if (d->name && strbuf_append(sb, d->name) != 0) return -1;
        break;

    case DIR_REWRITE_BASE:
        if (strbuf_append(sb, "RewriteBase ") != 0) return -1;
        if (d->value && strbuf_append(sb, d->value) != 0) return -1;
        break;

    case DIR_REWRITE_COND:
        if (strbuf_append(sb, "RewriteCond ") != 0) return -1;
        if (d->name && strbuf_append(sb, d->name) != 0) return -1;
        if (strbuf_append(sb, " ") != 0) return -1;
        if (d->data.rewrite_cond.cond_pattern &&
            strbuf_append(sb, d->data.rewrite_cond.cond_pattern) != 0)
            return -1;
        /* Use stored raw flags for lossless round-trip; fallback to
         * reconstructing from parsed booleans if flags_raw is NULL */
        if (d->data.rewrite_cond.flags_raw) {
            if (strbuf_append(sb, " ") != 0) return -1;
            if (strbuf_append(sb, d->data.rewrite_cond.flags_raw) != 0)
                return -1;
        } else if (d->data.rewrite_cond.nocase || d->data.rewrite_cond.or_next) {
            if (strbuf_append(sb, " [") != 0) return -1;
            int need_comma = 0;
            if (d->data.rewrite_cond.nocase) {
                if (strbuf_append(sb, "NC") != 0) return -1;
                need_comma = 1;
            }
            if (d->data.rewrite_cond.or_next) {
                if (need_comma && strbuf_append(sb, ",") != 0) return -1;
                if (strbuf_append(sb, "OR") != 0) return -1;
            }
            if (strbuf_append(sb, "]") != 0) return -1;
        }
        break;

    case DIR_REWRITE_RULE:
        /* Print associated conditions first */
        for (const htaccess_directive_t *cond = d->data.rewrite_rule.conditions;
             cond; cond = cond->next) {
            if (print_directive(sb, cond) != 0) return -1;
            if (strbuf_append(sb, "\n") != 0) return -1;
        }
        if (strbuf_append(sb, "RewriteRule ") != 0) return -1;
        if (d->data.rewrite_rule.pattern &&
            strbuf_append(sb, d->data.rewrite_rule.pattern) != 0)
            return -1;
        if (strbuf_append(sb, " ") != 0) return -1;
        if (d->value && strbuf_append(sb, d->value) != 0) return -1;
        if (d->data.rewrite_rule.flags_raw) {
            if (strbuf_append(sb, " ") != 0) return -1;
            if (strbuf_append(sb, d->data.rewrite_rule.flags_raw) != 0) return -1;
        }
        break;

    /* --- Phase 1 directives --- */
    case DIR_ADD_DEFAULT_CHARSET:
        if (strbuf_append(sb, "AddDefaultCharset ") != 0) return -1;
        if (d->value && strbuf_append(sb, d->value) != 0) return -1;
        break;

    case DIR_DEFAULT_TYPE:
        if (strbuf_append(sb, "DefaultType ") != 0) return -1;
        if (d->value && strbuf_append(sb, d->value) != 0) return -1;
        break;

    case DIR_REMOVE_TYPE:
        if (strbuf_append(sb, "RemoveType ") != 0) return -1;
        if (d->value && strbuf_append(sb, d->value) != 0) return -1;
        break;

    case DIR_REMOVE_HANDLER:
        if (strbuf_append(sb, "RemoveHandler ") != 0) return -1;
        if (d->value && strbuf_append(sb, d->value) != 0) return -1;
        break;

    case DIR_ACTION:
        if (strbuf_append(sb, "Action ") != 0) return -1;
        if (d->name && strbuf_append(sb, d->name) != 0) return -1;
        if (d->value) {
            if (strbuf_append(sb, " ") != 0) return -1;
            if (strbuf_append(sb, d->value) != 0) return -1;
        }
        break;

    case DIR_SATISFY:
        if (strbuf_append(sb, "Satisfy ") != 0) return -1;
        if (d->value && strbuf_append(sb, d->value) != 0) return -1;
        break;

    /* --- RewriteOptions --- */
    case DIR_REWRITE_OPTIONS:
        if (strbuf_append(sb, "RewriteOptions ") != 0) return -1;
        if (d->value && strbuf_append(sb, d->value) != 0) return -1;
        break;

    /* --- RewriteMap --- */
    case DIR_REWRITE_MAP:
        if (strbuf_append(sb, "RewriteMap ") != 0) return -1;
        if (d->data.rewrite_map.map_name)
            if (strbuf_append(sb, d->data.rewrite_map.map_name) != 0) return -1;
        if (d->data.rewrite_map.map_type) {
            if (strbuf_append(sb, " ") != 0) return -1;
            if (strbuf_append(sb, d->data.rewrite_map.map_type) != 0) return -1;
            if (d->data.rewrite_map.map_source) {
                if (strbuf_append(sb, ":") != 0) return -1;
                if (strbuf_append(sb, d->data.rewrite_map.map_source) != 0) return -1;
            }
        }
        break;

    /* --- If/ElseIf/Else conditional blocks --- */
    case DIR_IF:
    case DIR_ELSEIF: {
        const char *tag = (d->type == DIR_IF) ? "<If" : "<ElseIf";
        if (strbuf_append(sb, tag) != 0) return -1;
        if (d->data.if_block.condition) {
            char *expr_str = expr_to_string((const expr_node_t *)d->data.if_block.condition);
            if (expr_str) {
                if (strbuf_append(sb, " \"") != 0) { free(expr_str); return -1; }
                if (strbuf_append(sb, expr_str) != 0) { free(expr_str); return -1; }
                if (strbuf_append(sb, "\"") != 0) { free(expr_str); return -1; }
                free(expr_str);
            }
        }
        if (strbuf_append(sb, ">\n") != 0) return -1;
        for (const htaccess_directive_t *child = d->data.if_block.children;
             child; child = child->next) {
            if (print_directive(sb, child) != 0) return -1;
            if (strbuf_append(sb, "\n") != 0) return -1;
        }
        if (d->type == DIR_IF) {
            if (strbuf_append(sb, "</If>") != 0) return -1;
        } else {
            if (strbuf_append(sb, "</ElseIf>") != 0) return -1;
        }
        break;
    }

    case DIR_ELSE:
        if (strbuf_append(sb, "<Else>\n") != 0) return -1;
        for (const htaccess_directive_t *child = d->data.if_block.children;
             child; child = child->next) {
            if (print_directive(sb, child) != 0) return -1;
            if (strbuf_append(sb, "\n") != 0) return -1;
        }
        if (strbuf_append(sb, "</Else>") != 0) return -1;
        break;

    default:
        /* Unknown directive type — skip silently */
        break;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

char *htaccess_print(const htaccess_directive_t *head)
{
    if (!head)
        return NULL;

    strbuf_t sb;
    if (strbuf_init(&sb, 256) != 0)
        return NULL;

    int first = 1;
    for (const htaccess_directive_t *d = head; d; d = d->next) {
        if (!first) {
            if (strbuf_append(&sb, "\n") != 0) {
                free(sb.buf);
                return NULL;
            }
        }
        first = 0;

        if (print_directive(&sb, d) != 0) {
            free(sb.buf);
            return NULL;
        }
    }

    /* Append trailing newline */
    if (strbuf_append(&sb, "\n") != 0) {
        free(sb.buf);
        return NULL;
    }

    return sb.buf;
}
