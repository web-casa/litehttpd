/**
 * htaccess_parser.c - .htaccess file parser implementation
 *
 * Parses .htaccess content line-by-line into a linked list of
 * htaccess_directive_t nodes. Supports all 28 directive types and
 * nested FilesMatch blocks.
 *
 * Validates: Requirements 2.1, 2.2, 2.3, 9.1
 */
#include "htaccess_parser.h"
#include "htaccess_expr.h"
#include "htaccess_expires.h"
#include "ls.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/** Skip leading whitespace. */
static const char *skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p))
        p++;
    return p;
}

/** Skip trailing whitespace in-place and return trimmed length. */
static size_t trimmed_len(const char *s, size_t len)
{
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        len--;
    return len;
}

/**
 * Case-insensitive match of a keyword at position p.
 * Returns pointer past keyword if matched (followed by space or NUL),
 * or NULL on mismatch.
 */
static const char *match_kw(const char *p, const char *kw)
{
    size_t n = strlen(kw);
    if (strncasecmp(p, kw, n) != 0)
        return NULL;
    if (p[n] != '\0' && !isspace((unsigned char)p[n]))
        return NULL;
    return p + n;
}

/**
 * Extract the next whitespace-delimited token from *pp.
 * Returns a strdup'd copy, advances *pp past the token.
 * Returns NULL if no token available.
 */
static char *next_token(const char **pp)
{
    const char *p = skip_ws(*pp);
    if (!*p)
        return NULL;

    const char *start = p;
    /* Handle quoted token */
    if (*p == '"') {
        start = ++p;
        while (*p && *p != '"')
            p++;
        char *tok = strndup(start, (size_t)(p - start));
        if (*p == '"')
            p++;
        *pp = p;
        return tok;
    }

    while (*p && !isspace((unsigned char)*p))
        p++;
    char *tok = strndup(start, (size_t)(p - start));
    *pp = p;
    return tok;
}

/**
 * Get the rest of the line (trimmed) as a strdup'd string.
 * Returns NULL if nothing remains.
 */
static char *rest_of_line(const char **pp)
{
    const char *p = skip_ws(*pp);
    if (!*p)
        return NULL;

    size_t len = strlen(p);
    len = trimmed_len(p, len);
    if (len == 0)
        return NULL;

    /* Handle quoted rest */
    if (*p == '"' && len >= 2 && p[len - 1] == '"') {
        char *s = strndup(p + 1, len - 2);
        *pp = p + len;
        return s;
    }

    char *s = strndup(p, len);
    *pp = p + len;
    return s;
}

/**
 * Get the rest of the line (trimmed) as a strdup'd string, preserving quotes.
 * Used for ErrorDocument values where the leading quote is semantically significant.
 */
static char *rest_of_line_raw(const char **pp)
{
    const char *p = skip_ws(*pp);
    if (!*p)
        return NULL;
    size_t len = strlen(p);
    len = trimmed_len(p, len);
    if (len == 0)
        return NULL;
    char *s = strndup(p, len);
    *pp = p + len;
    return s;
}

/** Allocate a zeroed directive node. */
static htaccess_directive_t *alloc_directive(directive_type_t type, int line)
{
    htaccess_directive_t *d = (htaccess_directive_t *)calloc(1, sizeof(*d));
    if (d) {
        d->type = type;
        d->line_number = line;
    }
    return d;
}

/** Append directive to tail of list. */
static void append_directive(htaccess_directive_t **head,
                             htaccess_directive_t **tail,
                             htaccess_directive_t *node)
{
    if (!*head) {
        *head = node;
        *tail = node;
    } else {
        (*tail)->next = node;
        *tail = node;
    }
}

/* ------------------------------------------------------------------ */
/*  Individual directive parsers                                        */
/*  Each returns a directive node or NULL on parse failure.             */
/* ------------------------------------------------------------------ */

/**
 * Extract env=VAR or env=!VAR condition from the end of a value string.
 * Modifies the value string in-place (truncates). Returns strdup'd condition
 * name (e.g. "HTTPS" or "!NOSSL"), or NULL if no env= condition found.
 */
static char *extract_env_condition(char *value)
{
    if (!value)
        return NULL;

    /* Find the last closing quote to determine where the value ends.
     * " env=" inside a quoted value is literal, not a condition suffix.
     * Only match " env=" that appears AFTER the last closing quote. */
    char *search_start = value;
    char *last_quote = strrchr(value, '"');
    if (last_quote)
        search_start = last_quote + 1;

    /* Find " env=" in the suffix area only */
    char *env_pos = strstr(search_start, " env=");
    if (!env_pos) {
        /* Also check without leading space if search_start is at beginning */
        if (search_start == value) {
            /* No quotes at all — search the whole string */
            char *s = value;
            while ((s = strstr(s, " env=")) != NULL) {
                env_pos = s;
                s += 5;
            }
        }
        if (!env_pos)
            return NULL;
    }

    /* Extract the var name (everything after "env=") */
    char *var_start = env_pos + 5; /* skip " env=" */
    if (*var_start == '\0')
        return NULL;

    /* Trim trailing whitespace from var name */
    size_t var_len = strlen(var_start);
    while (var_len > 0 && isspace((unsigned char)var_start[var_len - 1]))
        var_len--;
    if (var_len == 0)
        return NULL;

    /* Reject if var name contains spaces (env= must be the final token) */
    for (size_t i = 0; i < var_len; i++) {
        if (isspace((unsigned char)var_start[i]))
            return NULL;
    }

    char *condition = strndup(var_start, var_len);

    /* Truncate value at the env= position, trimming trailing spaces */
    *env_pos = '\0';
    size_t len = strlen(value);
    while (len > 0 && isspace((unsigned char)value[len - 1]))
        value[--len] = '\0';

    /* Strip surrounding quotes if present after truncation */
    if (len >= 2 && value[0] == '"' && value[len - 1] == '"') {
        memmove(value, value + 1, len - 2);
        value[len - 2] = '\0';
    }

    return condition;
}

/**
 * Parse: Header [always] set|unset|append|merge|add|edit|edit* <name> [<value>] [env=VAR]
 */
static htaccess_directive_t *parse_header(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *action = next_token(&p);
    if (!action)
        return NULL;

    /* Check for "always" modifier */
    int always = 0;
    if (strcasecmp(action, "always") == 0) {
        always = 1;
        free(action);
        action = next_token(&p);
        if (!action)
            return NULL;
    }

    directive_type_t type;
    int needs_value = 1;
    int is_edit = 0;

    if (strcasecmp(action, "set") == 0)
        type = always ? DIR_HEADER_ALWAYS_SET : DIR_HEADER_SET;
    else if (strcasecmp(action, "unset") == 0) {
        type = always ? DIR_HEADER_ALWAYS_UNSET : DIR_HEADER_UNSET;
        needs_value = 0;
    } else if (strcasecmp(action, "append") == 0)
        type = always ? DIR_HEADER_ALWAYS_APPEND : DIR_HEADER_APPEND;
    else if (strcasecmp(action, "merge") == 0)
        type = always ? DIR_HEADER_ALWAYS_MERGE : DIR_HEADER_MERGE;
    else if (strcasecmp(action, "add") == 0)
        type = always ? DIR_HEADER_ALWAYS_ADD : DIR_HEADER_ADD;
    else if (strcasecmp(action, "edit") == 0) {
        type = always ? DIR_HEADER_ALWAYS_EDIT : DIR_HEADER_EDIT;
        is_edit = 1;
    } else if (strcasecmp(action, "edit*") == 0) {
        type = always ? DIR_HEADER_ALWAYS_EDIT_STAR : DIR_HEADER_EDIT_STAR;
        is_edit = 1;
    } else {
        free(action);
        return NULL;
    }
    free(action);

    char *name = next_token(&p);
    if (!name)
        return NULL;

    char *value = NULL;
    char *edit_pattern = NULL;
    char *env_cond = NULL;

    if (is_edit) {
        /* edit/edit*: need pattern and replacement tokens */
        edit_pattern = next_token(&p);
        if (!edit_pattern) { free(name); return NULL; }
        value = next_token(&p);
        if (!value) { free(name); free(edit_pattern); return NULL; }
        /* Check remaining for env= condition */
        const char *remaining = skip_ws(p);
        if (remaining && strncasecmp(remaining, "env=", 4) == 0) {
            const char *var = remaining + 4;
            if (*var != '\0') {
                /* Trim trailing whitespace */
                size_t vlen = strlen(var);
                while (vlen > 0 && isspace((unsigned char)var[vlen - 1]))
                    vlen--;
                if (vlen > 0)
                    env_cond = strndup(var, vlen);
            }
        }
    } else if (needs_value) {
        value = rest_of_line(&p);
        if (!value) {
            free(name);
            return NULL;
        }
        env_cond = extract_env_condition(value);
    } else {
        /* unset: check rest of line for env= condition */
        const char *remaining = skip_ws(p);
        if (remaining && strncasecmp(remaining, "env=", 4) == 0) {
            const char *var = remaining + 4;
            if (*var != '\0') {
                /* Trim trailing whitespace from var */
                size_t vlen = strlen(var);
                while (vlen > 0 && isspace((unsigned char)var[vlen - 1]))
                    vlen--;
                if (vlen > 0)
                    env_cond = strndup(var, vlen);
            }
        }
    }

    htaccess_directive_t *d = alloc_directive(type, line);
    if (!d) {
        free(name);
        free(value);
        free(edit_pattern);
        free(env_cond);
        return NULL;
    }
    d->name = name;
    d->value = value;
    d->env_condition = env_cond;
    if (is_edit)
        d->data.header_ext.edit_pattern = edit_pattern;
    return d;
}

/**
 * Parse: RequestHeader set|unset <name> [<value>]
 */
static htaccess_directive_t *parse_request_header(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *action = next_token(&p);
    if (!action)
        return NULL;

    directive_type_t type;
    int needs_value = 1;

    if (strcasecmp(action, "set") == 0)
        type = DIR_REQUEST_HEADER_SET;
    else if (strcasecmp(action, "unset") == 0) {
        type = DIR_REQUEST_HEADER_UNSET;
        needs_value = 0;
    } else {
        free(action);
        return NULL;
    }
    free(action);

    char *name = next_token(&p);
    if (!name)
        return NULL;

    char *value = NULL;
    if (needs_value) {
        value = rest_of_line(&p);
        if (!value) {
            free(name);
            return NULL;
        }
    }

    htaccess_directive_t *d = alloc_directive(type, line);
    if (!d) {
        free(name);
        free(value);
        return NULL;
    }
    d->name = name;
    d->value = value;
    return d;
}

/**
 * Parse: php_value <name> <value>
 */
static htaccess_directive_t *parse_php_value(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *name = next_token(&p);
    if (!name)
        return NULL;
    char *value = rest_of_line(&p);
    if (!value) {
        free(name);
        return NULL;
    }
    htaccess_directive_t *d = alloc_directive(DIR_PHP_VALUE, line);
    if (!d) { free(name); free(value); return NULL; }
    d->name = name;
    d->value = value;
    return d;
}

/**
 * Parse: php_flag <name> <on|off>
 */
static htaccess_directive_t *parse_php_flag(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *name = next_token(&p);
    if (!name)
        return NULL;
    char *value = next_token(&p);
    if (!value) {
        free(name);
        return NULL;
    }
    if (strcasecmp(value, "on") != 0 && strcasecmp(value, "off") != 0) {
        free(name);
        free(value);
        return NULL;
    }
    htaccess_directive_t *d = alloc_directive(DIR_PHP_FLAG, line);
    if (!d) { free(name); free(value); return NULL; }
    d->name = name;
    d->value = value;
    return d;
}

/**
 * Parse: php_admin_value <name> <value>
 */
static htaccess_directive_t *parse_php_admin_value(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *name = next_token(&p);
    if (!name)
        return NULL;
    char *value = rest_of_line(&p);
    if (!value) {
        free(name);
        return NULL;
    }
    htaccess_directive_t *d = alloc_directive(DIR_PHP_ADMIN_VALUE, line);
    if (!d) { free(name); free(value); return NULL; }
    d->name = name;
    d->value = value;
    return d;
}

/**
 * Parse: php_admin_flag <name> <on|off>
 */
static htaccess_directive_t *parse_php_admin_flag(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *name = next_token(&p);
    if (!name)
        return NULL;
    char *value = next_token(&p);
    if (!value) {
        free(name);
        return NULL;
    }
    if (strcasecmp(value, "on") != 0 && strcasecmp(value, "off") != 0) {
        free(name);
        free(value);
        return NULL;
    }
    htaccess_directive_t *d = alloc_directive(DIR_PHP_ADMIN_FLAG, line);
    if (!d) { free(name); free(value); return NULL; }
    d->name = name;
    d->value = value;
    return d;
}

/**
 * Parse: Order Allow,Deny | Order Deny,Allow
 */
static htaccess_directive_t *parse_order(const char *args, int line)
{
    const char *p = skip_ws(args);
    htaccess_directive_t *d = alloc_directive(DIR_ORDER, line);
    if (!d)
        return NULL;

    if ((strncasecmp(p, "Allow,Deny", 10) == 0) &&
        (p[10] == '\0' || p[10] == ' ' || p[10] == '\t')) {
        d->data.acl.order = ORDER_ALLOW_DENY;
    } else if ((strncasecmp(p, "Deny,Allow", 10) == 0) &&
               (p[10] == '\0' || p[10] == ' ' || p[10] == '\t')) {
        d->data.acl.order = ORDER_DENY_ALLOW;
    } else {
        free(d);
        return NULL;
    }
    return d;
}

/**
 * Parse: Allow from <cidr|all>
 */
static htaccess_directive_t *parse_allow_from(const char *args, int line)
{
    const char *p = skip_ws(args);
    const char *after = match_kw(p, "from");
    if (!after)
        return NULL;
    p = skip_ws(after);
    char *value = next_token(&p);
    if (!value)
        return NULL;

    htaccess_directive_t *d = alloc_directive(DIR_ALLOW_FROM, line);
    if (!d) { free(value); return NULL; }
    d->value = value;
    return d;
}

/**
 * Parse: Deny from <cidr|all>
 */
static htaccess_directive_t *parse_deny_from(const char *args, int line)
{
    const char *p = skip_ws(args);
    const char *after = match_kw(p, "from");
    if (!after)
        return NULL;
    p = skip_ws(after);
    char *value = next_token(&p);
    if (!value)
        return NULL;

    htaccess_directive_t *d = alloc_directive(DIR_DENY_FROM, line);
    if (!d) { free(value); return NULL; }
    d->value = value;
    return d;
}

/**
 * Map a redirect keyword to a status code.
 * Returns the status code (301/302/303/410) or 0 if not a keyword.
 */
static int resolve_redirect_keyword(const char *token)
{
    if (strcasecmp(token, "permanent") == 0) return 301;
    if (strcasecmp(token, "temp") == 0)      return 302;
    if (strcasecmp(token, "seeother") == 0)  return 303;
    if (strcasecmp(token, "gone") == 0)       return 410;
    return 0;
}

/**
 * Parse: Redirect [status|keyword] <path> [<url>]
 * Keywords: permanent(301), temp(302), seeother(303), gone(410)
 * "gone" does not require a target URL.
 */
static htaccess_directive_t *parse_redirect(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *tok1 = next_token(&p);
    if (!tok1)
        return NULL;

    int status_code = 302; /* default */
    char *path = NULL;
    char *url = NULL;

    /* Check if first token is a numeric status code */
    char *endp;
    long code = strtol(tok1, &endp, 10);
    if (*endp == '\0' && code >= 100 && code <= 599) {
        status_code = (int)code;
        path = next_token(&p);
        if (!path) {
            free(tok1);
            return NULL;
        }
        if (status_code == 410) {
            url = rest_of_line(&p); /* optional for gone */
        } else {
            url = rest_of_line(&p);
            if (!url) {
                free(tok1);
                free(path);
                return NULL;
            }
        }
        free(tok1);
    } else {
        /* Check if first token is a keyword */
        int kw_code = resolve_redirect_keyword(tok1);
        if (kw_code > 0) {
            status_code = kw_code;
            free(tok1);
            path = next_token(&p);
            if (!path)
                return NULL;
            if (kw_code == 410) {
                url = rest_of_line(&p); /* optional for gone */
            } else {
                url = rest_of_line(&p);
                if (!url) {
                    free(path);
                    return NULL;
                }
            }
        } else {
            /* First token is the path */
            path = tok1;
            url = rest_of_line(&p);
            if (!url) {
                free(path);
                return NULL;
            }
        }
    }

    htaccess_directive_t *d = alloc_directive(DIR_REDIRECT, line);
    if (!d) { free(path); free(url); return NULL; }
    d->name = path;
    d->value = url;
    d->data.redirect.status_code = status_code;
    d->data.redirect.pattern = NULL;
    return d;
}

/**
 * Parse: RedirectMatch [status|keyword] <pattern> [<url>]
 * Keywords: permanent(301), temp(302), seeother(303), gone(410)
 * "gone" does not require a target URL.
 */
static htaccess_directive_t *parse_redirect_match(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *tok1 = next_token(&p);
    if (!tok1)
        return NULL;

    int status_code = 302;
    char *pattern = NULL;
    char *url = NULL;

    char *endp;
    long code = strtol(tok1, &endp, 10);
    if (*endp == '\0' && code >= 100 && code <= 599) {
        status_code = (int)code;
        pattern = next_token(&p);
        if (!pattern) {
            free(tok1);
            return NULL;
        }
        if (status_code == 410) {
            url = rest_of_line(&p); /* optional for gone */
        } else {
            url = rest_of_line(&p);
            if (!url) {
                free(tok1);
                free(pattern);
                return NULL;
            }
        }
        free(tok1);
    } else {
        int kw_code = resolve_redirect_keyword(tok1);
        if (kw_code > 0) {
            status_code = kw_code;
            free(tok1);
            pattern = next_token(&p);
            if (!pattern)
                return NULL;
            if (kw_code == 410) {
                url = rest_of_line(&p); /* optional for gone */
            } else {
                url = rest_of_line(&p);
                if (!url) {
                    free(pattern);
                    return NULL;
                }
            }
        } else {
            pattern = tok1;
            url = rest_of_line(&p);
            if (!url) {
                free(pattern);
                return NULL;
            }
        }
    }

    htaccess_directive_t *d = alloc_directive(DIR_REDIRECT_MATCH, line);
    if (!d) { free(pattern); free(url); return NULL; }
    d->value = url;
    d->data.redirect.status_code = status_code;
    d->data.redirect.pattern = pattern;
    return d;
}

/**
 * Parse: ErrorDocument <code> <path|url|"message">
 */
static htaccess_directive_t *parse_error_document(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *code_str = next_token(&p);
    if (!code_str)
        return NULL;

    char *endp;
    long code = strtol(code_str, &endp, 10);
    if (*endp != '\0' || code < 100 || code > 599) {
        free(code_str);
        return NULL;
    }
    free(code_str);

    char *value = rest_of_line_raw(&p);
    if (!value)
        return NULL;

    /* Security: block absolute URLs for 401 error pages.
     * Prevents external auth redirect hijacking (Apache behavior). */
    if (code == 401 && value[0] != '"' &&
        (strncmp(value, "http://", 7) == 0 || strncmp(value, "https://", 8) == 0)) {
        free(value);
        return NULL;
    }

    htaccess_directive_t *d = alloc_directive(DIR_ERROR_DOCUMENT, line);
    if (!d) { free(value); return NULL; }
    d->value = value;
    d->data.error_doc.error_code = (int)code;

    return d;
}

/**
 * Parse: ExpiresActive On|Off
 */
static htaccess_directive_t *parse_expires_active(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *val = next_token(&p);
    if (!val)
        return NULL;

    int active;
    if (strcasecmp(val, "on") == 0)
        active = 1;
    else if (strcasecmp(val, "off") == 0)
        active = 0;
    else {
        free(val);
        return NULL;
    }
    free(val);

    htaccess_directive_t *d = alloc_directive(DIR_EXPIRES_ACTIVE, line);
    if (!d)
        return NULL;
    d->data.expires.active = active;
    return d;
}

/**
 * Parse: ExpiresByType <mime-type> "access plus N unit"
 */
static htaccess_directive_t *parse_expires_by_type(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *mime = next_token(&p);
    if (!mime)
        return NULL;

    char *duration_str = rest_of_line(&p);
    if (!duration_str) {
        free(mime);
        return NULL;
    }

    long secs = parse_expires_duration(duration_str);
    if (secs < 0) {
        free(mime);
        free(duration_str);
        return NULL;
    }

    htaccess_directive_t *d = alloc_directive(DIR_EXPIRES_BY_TYPE, line);
    if (!d) { free(mime); free(duration_str); return NULL; }
    d->name = mime;
    d->value = duration_str;
    d->data.expires.duration_sec = secs;
    return d;
}

/**
 * Parse: ExpiresDefault "access plus N unit"
 */
static htaccess_directive_t *parse_expires_default(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *duration_str = rest_of_line(&p);
    if (!duration_str)
        return NULL;

    long secs = parse_expires_duration(duration_str);
    if (secs < 0) {
        free(duration_str);
        return NULL;
    }

    htaccess_directive_t *d = alloc_directive(DIR_EXPIRES_DEFAULT, line);
    if (!d) { free(duration_str); return NULL; }
    d->value = duration_str;
    d->data.expires.duration_sec = secs;
    return d;
}

/**
 * Parse: SetEnv <name> <value>
 */
static htaccess_directive_t *parse_setenv(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *name = next_token(&p);
    if (!name)
        return NULL;
    char *value = rest_of_line(&p);
    if (!value) {
        free(name);
        return NULL;
    }
    htaccess_directive_t *d = alloc_directive(DIR_SETENV, line);
    if (!d) { free(name); free(value); return NULL; }
    d->name = name;
    d->value = value;
    return d;
}

/**
 * Parse: SetEnvIf/SetEnvIfNoCase <attribute> <pattern> <name>=<value>
 */
static htaccess_directive_t *parse_setenvif_typed(const char *args, int line,
                                                  directive_type_t type)
{
    const char *p = skip_ws(args);
    char *attribute = next_token(&p);
    if (!attribute)
        return NULL;
    char *pattern = next_token(&p);
    if (!pattern) {
        free(attribute);
        return NULL;
    }
    char *assignment = rest_of_line(&p);
    if (!assignment) {
        free(attribute);
        free(pattern);
        return NULL;
    }

    /* Split assignment on '=' */
    char *eq = strchr(assignment, '=');
    char *name = NULL;
    char *value = NULL;
    if (eq) {
        name = strndup(assignment, (size_t)(eq - assignment));
        value = strdup(eq + 1);
    } else {
        /* No '=', treat entire assignment as name with empty value */
        name = strdup(assignment);
        value = strdup("");
    }
    free(assignment);

    htaccess_directive_t *d = alloc_directive(type, line);
    if (!d) { free(attribute); free(pattern); free(name); free(value); return NULL; }
    d->name = name;
    d->value = value;
    d->data.envif.attribute = attribute;
    d->data.envif.pattern = pattern;
    return d;
}

/**
 * Parse: BrowserMatch <pattern> <name>=<value>
 */
static htaccess_directive_t *parse_browser_match(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *pattern = next_token(&p);
    if (!pattern)
        return NULL;
    char *assignment = rest_of_line(&p);
    if (!assignment) {
        free(pattern);
        return NULL;
    }

    char *eq = strchr(assignment, '=');
    char *name = NULL;
    char *value = NULL;
    if (eq) {
        name = strndup(assignment, (size_t)(eq - assignment));
        value = strdup(eq + 1);
    } else {
        name = strdup(assignment);
        value = strdup("");
    }
    free(assignment);

    htaccess_directive_t *d = alloc_directive(DIR_BROWSER_MATCH, line);
    if (!d) { free(pattern); free(name); free(value); return NULL; }
    d->name = name;
    d->value = value;
    d->data.envif.attribute = strdup("User-Agent");
    d->data.envif.pattern = pattern;
    return d;
}

/**
 * Parse: BruteForceProtection On|Off
 */
static htaccess_directive_t *parse_brute_force_protection(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *val = next_token(&p);
    if (!val)
        return NULL;

    int enabled;
    if (strcasecmp(val, "on") == 0)
        enabled = 1;
    else if (strcasecmp(val, "off") == 0)
        enabled = 0;
    else {
        free(val);
        return NULL;
    }
    free(val);

    htaccess_directive_t *d = alloc_directive(DIR_BRUTE_FORCE_PROTECTION, line);
    if (!d)
        return NULL;
    d->data.brute_force.enabled = enabled;
    return d;
}

/**
 * Parse: BruteForceAllowedAttempts <N>
 */
static htaccess_directive_t *parse_brute_force_attempts(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *val = next_token(&p);
    if (!val)
        return NULL;

    char *endp;
    long n = strtol(val, &endp, 10);
    if (*endp != '\0' || n <= 0) {
        free(val);
        return NULL;
    }
    free(val);

    htaccess_directive_t *d = alloc_directive(DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS, line);
    if (!d)
        return NULL;
    d->data.brute_force.allowed_attempts = (int)n;
    return d;
}

/**
 * Parse: BruteForceWindow <N>
 */
static htaccess_directive_t *parse_brute_force_window(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *val = next_token(&p);
    if (!val)
        return NULL;

    char *endp;
    long n = strtol(val, &endp, 10);
    if (*endp != '\0' || n <= 0) {
        free(val);
        return NULL;
    }
    free(val);

    htaccess_directive_t *d = alloc_directive(DIR_BRUTE_FORCE_WINDOW, line);
    if (!d)
        return NULL;
    d->data.brute_force.window_sec = (int)n;
    return d;
}

/**
 * Parse: BruteForceAction block|throttle
 */
static htaccess_directive_t *parse_brute_force_action(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *val = next_token(&p);
    if (!val)
        return NULL;

    bf_action_t action;
    if (strcasecmp(val, "block") == 0)
        action = BF_ACTION_BLOCK;
    else if (strcasecmp(val, "throttle") == 0)
        action = BF_ACTION_THROTTLE;
    else if (strcasecmp(val, "log") == 0)
        action = BF_ACTION_LOG;
    else {
        free(val);
        return NULL;
    }
    free(val);

    htaccess_directive_t *d = alloc_directive(DIR_BRUTE_FORCE_ACTION, line);
    if (!d)
        return NULL;
    d->data.brute_force.action = action;
    return d;
}

/**
 * Parse: Options [+|-]Flag1 [+|-]Flag2 ...
 * Supported flags: Indexes, FollowSymLinks, MultiViews, ExecCGI
 * Tri-state: +1 = enable, -1 = disable, 0 = unchanged
 */
static htaccess_directive_t *parse_options(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *flags_str = rest_of_line(&p);
    if (!flags_str)
        return NULL;

    htaccess_directive_t *d = alloc_directive(DIR_OPTIONS, line);
    if (!d) {
        free(flags_str);
        return NULL;
    }
    d->value = flags_str;

    /* Parse individual flags from the value string */
    const char *s = flags_str;
    while (*s) {
        s = skip_ws(s);
        if (!*s)
            break;

        int sign = 0;
        if (*s == '+') {
            sign = 1;
            s++;
        } else if (*s == '-') {
            sign = -1;
            s++;
        } else {
            /* No sign prefix — treat as enable */
            sign = 1;
        }

        /* Extract flag name */
        const char *start = s;
        while (*s && !isspace((unsigned char)*s))
            s++;
        size_t flen = (size_t)(s - start);

        if (flen == 7 && strncasecmp(start, "Indexes", 7) == 0) {
            d->data.options.indexes = sign;
        } else if (flen == 14 && strncasecmp(start, "FollowSymLinks", 14) == 0) {
            d->data.options.follow_symlinks = sign;
        } else if (flen == 10 && strncasecmp(start, "MultiViews", 10) == 0) {
            d->data.options.multiviews = sign;
        } else if (flen == 7 && strncasecmp(start, "ExecCGI", 7) == 0) {
            d->data.options.exec_cgi = sign;
        } else {
            /* Unknown flag — log WARN and ignore */
            lsi_log(NULL, LSI_LOG_WARN,
                    "[htaccess] line %d: unknown Options flag: %.*s",
                    line, (int)flen, start);
        }
    }

    return d;
}

/**
 * Parse: BruteForceThrottleDuration <N>
 */
static htaccess_directive_t *parse_brute_force_throttle(const char *args, int line)
{
    const char *p = skip_ws(args);
    char *val = next_token(&p);
    if (!val)
        return NULL;

    char *endp;
    long n = strtol(val, &endp, 10);
    if (*endp != '\0' || n <= 0) {
        free(val);
        return NULL;
    }
    free(val);

    htaccess_directive_t *d = alloc_directive(DIR_BRUTE_FORCE_THROTTLE_DURATION, line);
    if (!d)
        return NULL;
    d->data.brute_force.throttle_ms = (int)n;
    return d;
}

/**
 * Parse: Require all granted | Require all denied | Require ip <cidr>
 *        Require not ip <cidr> | Require valid-user
 */
static htaccess_directive_t *parse_require(const char *args, int line)
{
    const char *p = skip_ws(args);
    const char *after;

    /* Require all granted / Require all denied */
    after = match_kw(p, "all");
    if (after) {
        const char *sub = skip_ws(after);
        /* match_kw already checks word boundary (next char is space or \0),
         * but "granted"/"denied" also need boundary checking */
        if (strncasecmp(sub, "granted", 7) == 0 &&
            (sub[7] == '\0' || sub[7] == ' ' || sub[7] == '\t'))
            return alloc_directive(DIR_REQUIRE_ALL_GRANTED, line);
        if (strncasecmp(sub, "denied", 6) == 0 &&
            (sub[6] == '\0' || sub[6] == ' ' || sub[6] == '\t'))
            return alloc_directive(DIR_REQUIRE_ALL_DENIED, line);
        return NULL;
    }

    /* Require not ip <cidr-list> */
    after = match_kw(p, "not");
    if (after) {
        const char *sub = skip_ws(after);
        const char *after2 = match_kw(sub, "ip");
        if (after2) {
            char *val = rest_of_line(&after2);
            if (!val) return NULL;
            htaccess_directive_t *d = alloc_directive(DIR_REQUIRE_NOT_IP, line);
            if (!d) { free(val); return NULL; }
            d->value = val;
            return d;
        }
        return NULL;
    }

    /* Require ip <cidr-list> */
    after = match_kw(p, "ip");
    if (after) {
        char *val = rest_of_line(&after);
        if (!val) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_REQUIRE_IP, line);
        if (!d) { free(val); return NULL; }
        d->value = val;
        return d;
    }

    /* Require valid-user */
    after = match_kw(p, "valid-user");
    if (after)
        return alloc_directive(DIR_REQUIRE_VALID_USER, line);

    /* Require env VARNAME */
    after = match_kw(p, "env");
    if (after) {
        char *varname = next_token(&after);
        if (!varname) return NULL;
        /* Warn if multiple var names (only first is checked) */
        const char *rest = skip_ws(after);
        if (rest && *rest && *rest != '#')
            lsi_log(NULL, LSI_LOG_WARN,
                    "[htaccess] line %d: Require env only checks first var '%s', "
                    "trailing tokens ignored", line, varname);
        htaccess_directive_t *d = alloc_directive(DIR_REQUIRE_ENV, line);
        if (!d) { free(varname); return NULL; }
        d->name = varname;
        return d;
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Rewrite directive parsers                                          */
/* ------------------------------------------------------------------ */

/**
 * Parse "RewriteEngine On|Off".
 */
static htaccess_directive_t *parse_rewrite_engine(const char *args, int line)
{
    char *val = next_token(&args);
    if (!val)
        return NULL;
    htaccess_directive_t *d = alloc_directive(DIR_REWRITE_ENGINE, line);
    if (!d) { free(val); return NULL; }
    d->name = val;
    return d;
}

/**
 * Parse "RewriteBase /path/".
 */
static htaccess_directive_t *parse_rewrite_base(const char *args, int line)
{
    char *val = next_token(&args);
    if (!val)
        return NULL;
    htaccess_directive_t *d = alloc_directive(DIR_REWRITE_BASE, line);
    if (!d) { free(val); return NULL; }
    d->value = val;
    return d;
}

/**
 * Parse "RewriteCond TestString CondPattern [flags]".
 * TestString -> name, CondPattern -> data.rewrite_cond.cond_pattern
 * Flags: [NC], [OR], [NC,OR]
 */
static htaccess_directive_t *parse_rewrite_cond(const char *args, int line)
{
    char *test_str = next_token(&args);
    if (!test_str)
        return NULL;
    char *pattern = next_token(&args);
    if (!pattern) {
        free(test_str);
        return NULL;
    }

    htaccess_directive_t *d = alloc_directive(DIR_REWRITE_COND, line);
    if (!d) { free(test_str); free(pattern); return NULL; }
    d->name = test_str;
    d->data.rewrite_cond.cond_pattern = pattern;
    d->data.rewrite_cond.flags_raw = NULL;
    d->data.rewrite_cond.nocase = 0;
    d->data.rewrite_cond.or_next = 0;

    /* Parse optional [flags] — store raw string for lossless rebuild */
    const char *fp = skip_ws(args);
    if (*fp == '[') {
        const char *end = strchr(fp, ']');
        if (end) {
            /* Store the complete [flags] string for rebuild_rewrite_text */
            d->data.rewrite_cond.flags_raw = strndup(fp, (size_t)(end - fp + 1));
            /* flags_raw NULL on OOM is safe — fallback reconstruction
             * from nocase/or_next booleans handles this case */
            fp++;
            /* Scan comma-separated flags between [ and ] */
            while (fp < end) {
                const char *cp = skip_ws(fp);
                if (strncasecmp(cp, "NC", 2) == 0 &&
                    (cp[2] == ',' || cp[2] == ']' || cp[2] == '\0' || isspace((unsigned char)cp[2])))
                    d->data.rewrite_cond.nocase = 1;
                else if (strncasecmp(cp, "OR", 2) == 0 &&
                         (cp[2] == ',' || cp[2] == ']' || cp[2] == '\0' || isspace((unsigned char)cp[2])))
                    d->data.rewrite_cond.or_next = 1;
                /* Advance to next comma or end */
                const char *comma = memchr(cp, ',', (size_t)(end - cp));
                fp = comma ? comma + 1 : end;
            }
        }
    }

    return d;
}

/**
 * Parse "RewriteRule Pattern Substitution [flags]".
 * Pattern -> data.rewrite_rule.pattern
 * Substitution -> value
 * Flags -> data.rewrite_rule.flags_raw (raw string including brackets)
 */
static htaccess_directive_t *parse_rewrite_rule(const char *args, int line)
{
    char *pattern = next_token(&args);
    if (!pattern)
        return NULL;
    char *subst = next_token(&args);
    if (!subst) {
        free(pattern);
        return NULL;
    }

    htaccess_directive_t *d = alloc_directive(DIR_REWRITE_RULE, line);
    if (!d) { free(pattern); free(subst); return NULL; }
    d->data.rewrite_rule.pattern = pattern;
    d->value = subst;
    d->data.rewrite_rule.flags_raw = NULL;
    d->data.rewrite_rule.conditions = NULL;

    /* Parse optional [flags] -- store raw string for OLS pass-through */
    const char *fp = skip_ws(args);
    if (*fp == '[') {
        const char *end = strchr(fp, ']');
        if (end) {
            d->data.rewrite_rule.flags_raw = strndup(fp, (size_t)(end - fp + 1));
        } else {
            lsi_log(NULL, LSI_LOG_WARN,
                    "[htaccess] line %d: RewriteRule flags missing closing ']'",
                    line);
        }
    }

    return d;
}

/**
 * Parse: RewriteOptions inherit|IgnoreInherit
 */
static htaccess_directive_t *parse_rewrite_options(const char *args, int line)
{
    char *val = next_token(&args);
    if (!val) return NULL;
    htaccess_directive_t *d = alloc_directive(DIR_REWRITE_OPTIONS, line);
    if (!d) { free(val); return NULL; }
    d->value = val;
    return d;
}

/**
 * Parse: RewriteMap name type:source
 * Example: RewriteMap mymap txt:/etc/httpd/mymap.txt
 */
static htaccess_directive_t *parse_rewrite_map(const char *args, int line)
{
    char *name = next_token(&args);
    if (!name) return NULL;
    char *type_source = next_token(&args);
    if (!type_source) { free(name); return NULL; }

    /* Split type:source on ':' */
    char *colon = strchr(type_source, ':');
    if (!colon) {
        free(name);
        free(type_source);
        return NULL;
    }
    *colon = '\0';

    htaccess_directive_t *d = alloc_directive(DIR_REWRITE_MAP, line);
    if (!d) { free(name); free(type_source); return NULL; }
    d->data.rewrite_map.map_name = name;
    d->data.rewrite_map.map_type = strdup(type_source);
    d->data.rewrite_map.map_source = strdup(colon + 1);
    free(type_source);

    if (!d->data.rewrite_map.map_type || !d->data.rewrite_map.map_source) {
        /* OOM — free what we can */
        free(d->data.rewrite_map.map_name);
        free(d->data.rewrite_map.map_type);
        free(d->data.rewrite_map.map_source);
        free(d);
        return NULL;
    }
    return d;
}

/* ------------------------------------------------------------------ */
/*  Line dispatcher                                                    */
/* ------------------------------------------------------------------ */

/**
 * Try to parse a single non-empty, non-comment line into a directive.
 * Returns a directive node, or NULL if the line is not recognised.
 */
static htaccess_directive_t *parse_line(const char *line, int line_num)
{
    const char *p = skip_ws(line);
    const char *after;

    /* Header */
    after = match_kw(p, "Header");
    if (after)
        return parse_header(after, line_num);

    /* RequestHeader */
    after = match_kw(p, "RequestHeader");
    if (after)
        return parse_request_header(after, line_num);

    /* php_value */
    after = match_kw(p, "php_value");
    if (after)
        return parse_php_value(after, line_num);

    /* php_flag */
    after = match_kw(p, "php_flag");
    if (after)
        return parse_php_flag(after, line_num);

    /* php_admin_value */
    after = match_kw(p, "php_admin_value");
    if (after)
        return parse_php_admin_value(after, line_num);

    /* php_admin_flag */
    after = match_kw(p, "php_admin_flag");
    if (after)
        return parse_php_admin_flag(after, line_num);

    /* Order */
    after = match_kw(p, "Order");
    if (after)
        return parse_order(after, line_num);

    /* Allow */
    after = match_kw(p, "Allow");
    if (after)
        return parse_allow_from(after, line_num);

    /* Deny */
    after = match_kw(p, "Deny");
    if (after)
        return parse_deny_from(after, line_num);

    /* RedirectMatch (must check before Redirect) */
    after = match_kw(p, "RedirectMatch");
    if (after)
        return parse_redirect_match(after, line_num);

    /* Redirect */
    after = match_kw(p, "Redirect");
    if (after)
        return parse_redirect(after, line_num);

    /* ErrorDocument */
    after = match_kw(p, "ErrorDocument");
    if (after)
        return parse_error_document(after, line_num);

    /* ExpiresActive */
    after = match_kw(p, "ExpiresActive");
    if (after)
        return parse_expires_active(after, line_num);

    /* ExpiresByType */
    after = match_kw(p, "ExpiresByType");
    if (after)
        return parse_expires_by_type(after, line_num);

    /* ExpiresDefault */
    after = match_kw(p, "ExpiresDefault");
    if (after)
        return parse_expires_default(after, line_num);

    /* SetEnvIfNoCase (must check before SetEnvIf — prefix match) */
    after = match_kw(p, "SetEnvIfNoCase");
    if (after)
        return parse_setenvif_typed(after, line_num, DIR_SETENVIF_NOCASE);

    /* SetEnvIf (must check before SetEnv) */
    after = match_kw(p, "SetEnvIf");
    if (after)
        return parse_setenvif_typed(after, line_num, DIR_SETENVIF);

    /* SetEnv */
    after = match_kw(p, "SetEnv");
    if (after)
        return parse_setenv(after, line_num);

    /* BrowserMatch */
    after = match_kw(p, "BrowserMatch");
    if (after)
        return parse_browser_match(after, line_num);

    /* BruteForceProtection */
    after = match_kw(p, "BruteForceProtection");
    if (after)
        return parse_brute_force_protection(after, line_num);

    /* BruteForceAllowedAttempts */
    after = match_kw(p, "BruteForceAllowedAttempts");
    if (after)
        return parse_brute_force_attempts(after, line_num);

    /* BruteForceWindow */
    after = match_kw(p, "BruteForceWindow");
    if (after)
        return parse_brute_force_window(after, line_num);

    /* BruteForceAction */
    after = match_kw(p, "BruteForceAction");
    if (after)
        return parse_brute_force_action(after, line_num);

    /* BruteForceThrottleDuration */
    after = match_kw(p, "BruteForceThrottleDuration");
    if (after)
        return parse_brute_force_throttle(after, line_num);

    /* BruteForceXForwardedFor On|Off */
    after = match_kw(p, "BruteForceXForwardedFor");
    if (after) {
        char *val = next_token(&after);
        if (!val) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_BRUTE_FORCE_X_FORWARDED_FOR, line_num);
        if (!d) { free(val); return NULL; }
        d->data.brute_force.enabled = (strcasecmp(val, "On") == 0) ? 1 : 0;
        free(val);
        return d;
    }

    /* BruteForceWhitelist CIDR list */
    after = match_kw(p, "BruteForceWhitelist");
    if (after) {
        char *val = rest_of_line(&after);
        if (!val) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_BRUTE_FORCE_WHITELIST, line_num);
        if (!d) { free(val); return NULL; }
        d->value = val;
        return d;
    }

    /* BruteForceProtectPath URL path */
    after = match_kw(p, "BruteForceProtectPath");
    if (after) {
        char *val = next_token(&after);
        if (!val) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_BRUTE_FORCE_PROTECT_PATH, line_num);
        if (!d) { free(val); return NULL; }
        d->value = val;
        return d;
    }

    /* Options */
    after = match_kw(p, "Options");
    if (after)
        return parse_options(after, line_num);

    /* Require (must check before other R-keywords) */
    after = match_kw(p, "Require");
    if (after)
        return parse_require(after, line_num);

    /* AuthType */
    after = match_kw(p, "AuthType");
    if (after) {
        char *val = next_token(&after);
        if (!val) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_AUTH_TYPE, line_num);
        if (!d) { free(val); return NULL; }
        d->value = val;
        return d;
    }

    /* AuthName */
    after = match_kw(p, "AuthName");
    if (after) {
        char *val = rest_of_line(&after);
        if (!val) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_AUTH_NAME, line_num);
        if (!d) { free(val); return NULL; }
        d->value = val;
        return d;
    }

    /* AuthUserFile */
    after = match_kw(p, "AuthUserFile");
    if (after) {
        char *val = rest_of_line(&after);
        if (!val) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_AUTH_USER_FILE, line_num);
        if (!d) { free(val); return NULL; }
        d->value = val;
        return d;
    }

    /* AddHandler handler-name ext1 ext2 ... */
    after = match_kw(p, "AddHandler");
    if (after) {
        char *handler = next_token(&after);
        if (!handler) return NULL;
        char *exts = rest_of_line(&after);
        htaccess_directive_t *d = alloc_directive(DIR_ADD_HANDLER, line_num);
        if (!d) { free(handler); free(exts); return NULL; }
        d->name = handler;
        d->value = exts;  /* may be NULL if no extensions */
        return d;
    }

    /* SetHandler handler-name */
    after = match_kw(p, "SetHandler");
    if (after) {
        char *val = rest_of_line(&after);
        if (!val) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_SET_HANDLER, line_num);
        if (!d) { free(val); return NULL; }
        d->value = val;
        return d;
    }

    /* AddType mime-type ext1 ext2 ... */
    after = match_kw(p, "AddType");
    if (after) {
        char *mime = next_token(&after);
        if (!mime) return NULL;
        char *exts = rest_of_line(&after);
        htaccess_directive_t *d = alloc_directive(DIR_ADD_TYPE, line_num);
        if (!d) { free(mime); free(exts); return NULL; }
        d->name = mime;
        d->value = exts;  /* may be NULL if no extensions */
        return d;
    }

    /* DirectoryIndex file1 file2 ... */
    after = match_kw(p, "DirectoryIndex");
    if (after) {
        char *val = rest_of_line(&after);
        if (!val) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_DIRECTORY_INDEX, line_num);
        if (!d) { free(val); return NULL; }
        d->value = val;
        return d;
    }

    /* ForceType mime-type */
    after = match_kw(p, "ForceType");
    if (after) {
        char *val = next_token(&after);
        if (!val) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_FORCE_TYPE, line_num);
        if (!d) { free(val); return NULL; }
        d->value = val;
        return d;
    }

    /* AddEncoding encoding ext1 ext2 ... */
    after = match_kw(p, "AddEncoding");
    if (after) {
        char *enc = next_token(&after);
        if (!enc) return NULL;
        char *exts = rest_of_line(&after);
        htaccess_directive_t *d = alloc_directive(DIR_ADD_ENCODING, line_num);
        if (!d) { free(enc); free(exts); return NULL; }
        d->name = enc;
        d->value = exts;
        return d;
    }

    /* AddCharset charset ext1 ext2 ... */
    after = match_kw(p, "AddCharset");
    if (after) {
        char *cs = next_token(&after);
        if (!cs) return NULL;
        char *exts = rest_of_line(&after);
        htaccess_directive_t *d = alloc_directive(DIR_ADD_CHARSET, line_num);
        if (!d) { free(cs); free(exts); return NULL; }
        d->name = cs;
        d->value = exts;
        return d;
    }

    /* AddDefaultCharset charset|Off */
    after = match_kw(p, "AddDefaultCharset");
    if (after) {
        char *val = next_token(&after);
        if (!val) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_ADD_DEFAULT_CHARSET, line_num);
        if (!d) { free(val); return NULL; }
        d->value = val;
        return d;
    }

    /* DefaultType mime-type */
    after = match_kw(p, "DefaultType");
    if (after) {
        char *val = next_token(&after);
        if (!val) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_DEFAULT_TYPE, line_num);
        if (!d) { free(val); return NULL; }
        d->value = val;
        return d;
    }

    /* RemoveType .ext1 .ext2 ... */
    after = match_kw(p, "RemoveType");
    if (after) {
        char *exts = rest_of_line(&after);
        if (!exts) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_REMOVE_TYPE, line_num);
        if (!d) { free(exts); return NULL; }
        d->value = exts;
        return d;
    }

    /* RemoveHandler .ext1 .ext2 ... */
    after = match_kw(p, "RemoveHandler");
    if (after) {
        char *exts = rest_of_line(&after);
        if (!exts) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_REMOVE_HANDLER, line_num);
        if (!d) { free(exts); return NULL; }
        d->value = exts;
        return d;
    }

    /* Action media-type cgi-script [virtual] */
    after = match_kw(p, "Action");
    if (after) {
        char *mime = next_token(&after);
        if (!mime) return NULL;
        char *handler = next_token(&after);
        if (!handler) { free(mime); return NULL; }
        htaccess_directive_t *d = alloc_directive(DIR_ACTION, line_num);
        if (!d) { free(mime); free(handler); return NULL; }
        d->name = mime;
        d->value = handler;
        return d;
    }

    /* Satisfy Any|All */
    after = match_kw(p, "Satisfy");
    if (after) {
        char *val = next_token(&after);
        if (!val) return NULL;
        htaccess_directive_t *d = alloc_directive(DIR_SATISFY, line_num);
        if (!d) { free(val); return NULL; }
        d->value = val;
        return d;
    }

    /* RewriteEngine On|Off */
    after = match_kw(p, "RewriteEngine");
    if (after)
        return parse_rewrite_engine(after, line_num);

    /* RewriteBase /path/ */
    after = match_kw(p, "RewriteBase");
    if (after)
        return parse_rewrite_base(after, line_num);

    /* RewriteCond (must check before RewriteRule — both start with "Rewrite") */
    after = match_kw(p, "RewriteCond");
    if (after)
        return parse_rewrite_cond(after, line_num);

    /* RewriteRule */
    after = match_kw(p, "RewriteRule");
    if (after)
        return parse_rewrite_rule(after, line_num);

    /* RewriteOptions */
    after = match_kw(p, "RewriteOptions");
    if (after)
        return parse_rewrite_options(after, line_num);

    /* RewriteMap */
    after = match_kw(p, "RewriteMap");
    if (after)
        return parse_rewrite_map(after, line_num);

    return NULL; /* Unrecognised directive */
}

/* ------------------------------------------------------------------ */
/*  FilesMatch block detection helpers                                 */
/* ------------------------------------------------------------------ */

/**
 * Check if a line is a <FilesMatch "pattern"> opening tag.
 * If so, extracts the pattern into *out_pattern (caller must free).
 * Returns 1 if matched, 0 otherwise.
 */
static int is_files_match_open(const char *line, char **out_pattern)
{
    const char *p = skip_ws(line);
    if (*p != '<')
        return 0;
    p++;
    const char *after = match_kw(p, "FilesMatch");
    if (!after)
        return 0;
    p = skip_ws(after);

    /* Extract pattern (may be quoted) */
    if (*p == '"') {
        p++;
        const char *end = strchr(p, '"');
        if (!end)
            return 0;
        *out_pattern = strndup(p, (size_t)(end - p));
        /* Verify closing '>' follows */
        p = skip_ws(end + 1);
        if (*p != '>')
        {
            free(*out_pattern);
            *out_pattern = NULL;
            return 0;
        }
        return 1;
    }

    /* Unquoted pattern */
    const char *start = p;
    while (*p && *p != '>' && !isspace((unsigned char)*p))
        p++;
    if (p == start)
        return 0;
    *out_pattern = strndup(start, (size_t)(p - start));
    p = skip_ws(p);
    if (*p != '>') {
        free(*out_pattern);
        *out_pattern = NULL;
        return 0;
    }
    return 1;
}

/**
 * Check if a line is a </FilesMatch> closing tag.
 * Accepts both </FilesMatch> and </FilesMatch > (with optional space).
 */
static int is_files_match_close(const char *line)
{
    const char *p = skip_ws(line);
    if (*p != '<')
        return 0;
    p++;
    if (*p != '/')
        return 0;
    p++;

    /* Match "FilesMatch" keyword — accept '>' immediately after */
    size_t kw_len = strlen("FilesMatch");
    if (strncasecmp(p, "FilesMatch", kw_len) != 0)
        return 0;
    p += kw_len;
    p = skip_ws(p);
    return (*p == '>');
}

/* ------------------------------------------------------------------ */
/*  IfModule block detection helpers                                   */
/* ------------------------------------------------------------------ */

/**
 * Check if a line is an <IfModule [!]module_name> opening tag.
 * If so, extracts the module name (including any "!" prefix) into *out_module
 * and sets *negated to 1 if the "!" prefix is present.
 * Caller must free *out_module.
 * Returns 1 if matched, 0 otherwise.
 */
static int is_ifmodule_open(const char *line, char **out_module, int *negated)
{
    const char *p = skip_ws(line);
    if (*p != '<')
        return 0;
    p++;
    const char *after = match_kw(p, "IfModule");
    if (!after)
        return 0;
    p = skip_ws(after);

    /* Check for negation prefix */
    int neg = 0;
    if (*p == '!') {
        neg = 1;
        p++;
        /* Allow optional whitespace after '!' */
        p = skip_ws(p);
    }

    /* Extract module name (may be quoted) */
    const char *start = p;
    if (*p == '"') {
        p++;
        const char *end = strchr(p, '"');
        if (!end)
            return 0;
        start = p;
        p = end;
        /* Build module name with possible "!" prefix */
        size_t name_len = (size_t)(p - start);
        if (neg) {
            *out_module = (char *)malloc(name_len + 2);
            if (!*out_module) return 0;
            (*out_module)[0] = '!';
            memcpy(*out_module + 1, start, name_len);
            (*out_module)[name_len + 1] = '\0';
        } else {
            *out_module = strndup(start, name_len);
        }
        p++; /* skip closing quote */
        p = skip_ws(p);
        if (*p != '>') {
            free(*out_module);
            *out_module = NULL;
            return 0;
        }
        *negated = neg;
        return 1;
    }

    /* Unquoted module name */
    while (*p && *p != '>' && !isspace((unsigned char)*p))
        p++;
    if (p == start)
        return 0;

    size_t name_len = (size_t)(p - start);
    if (neg) {
        *out_module = (char *)malloc(name_len + 2);
        if (!*out_module) return 0;
        (*out_module)[0] = '!';
        memcpy(*out_module + 1, start, name_len);
        (*out_module)[name_len + 1] = '\0';
    } else {
        *out_module = strndup(start, name_len);
    }

    p = skip_ws(p);
    if (*p != '>') {
        free(*out_module);
        *out_module = NULL;
        return 0;
    }
    *negated = neg;
    return 1;
}

/**
 * Check if a line is a </IfModule> closing tag.
 * Accepts both </IfModule> and </IfModule > (with optional space).
 */
static int is_ifmodule_close(const char *line)
{
    const char *p = skip_ws(line);
    if (*p != '<')
        return 0;
    p++;
    if (*p != '/')
        return 0;
    p++;

    size_t kw_len = strlen("IfModule");
    if (strncasecmp(p, "IfModule", kw_len) != 0)
        return 0;
    p += kw_len;
    p = skip_ws(p);
    return (*p == '>');
}

/* ------------------------------------------------------------------ */
/*  If/ElseIf/Else block detection helpers                             */
/* ------------------------------------------------------------------ */

/**
 * Check if a line is <If "expression"> or <If expression>.
 * Extracts the expression string into *out_expr (caller must free).
 */
static int is_if_open(const char *line, char **out_expr)
{
    const char *p = skip_ws(line);
    if (*p != '<') return 0;
    p++;
    const char *after = match_kw(p, "If");
    if (!after) return 0;
    /* Avoid matching IfModule/IfDefine */
    if (after[0] && isalpha((unsigned char)after[0])) return 0;
    p = skip_ws(after);

    /* Find closing '>' */
    const char *end = strrchr(p, '>');
    if (!end || end <= p) return 0;

    /* Extract expression (strip outer quotes if present) */
    size_t len = (size_t)(end - p);
    while (len > 0 && isspace((unsigned char)p[len - 1])) len--;
    if (len >= 2 && p[0] == '"' && p[len - 1] == '"') {
        p++; len -= 2;
    }
    if (out_expr)
        *out_expr = strndup(p, len);
    return 1;
}

static int is_elseif_open(const char *line, char **out_expr)
{
    const char *p = skip_ws(line);
    if (*p != '<') return 0;
    p++;
    const char *after = match_kw(p, "ElseIf");
    if (!after) return 0;
    p = skip_ws(after);
    const char *end = strrchr(p, '>');
    if (!end || end <= p) return 0;
    size_t len = (size_t)(end - p);
    while (len > 0 && isspace((unsigned char)p[len - 1])) len--;
    if (len >= 2 && p[0] == '"' && p[len - 1] == '"') {
        p++; len -= 2;
    }
    if (out_expr)
        *out_expr = strndup(p, len);
    return 1;
}

static int is_else_open(const char *line)
{
    const char *p = skip_ws(line);
    if (*p != '<') return 0;
    p++;
    /* Case-insensitive match of "Else" */
    if (strncasecmp(p, "Else", 4) != 0) return 0;
    p += 4;
    /* Must NOT be ElseIf */
    if (p[0] && isalpha((unsigned char)p[0])) return 0;
    p = skip_ws(p);
    return (*p == '>');
}

static int is_if_close(const char *line)
{
    const char *p = skip_ws(line);
    if (*p != '<' || p[1] != '/') return 0;
    p += 2;
    size_t kw_len = 2;
    if (strncasecmp(p, "If", kw_len) != 0) return 0;
    p += kw_len;
    p = skip_ws(p);
    return (*p == '>');
}

static int is_elseif_close(const char *line)
{
    const char *p = skip_ws(line);
    if (*p != '<' || p[1] != '/') return 0;
    p += 2;
    if (strncasecmp(p, "ElseIf", 6) != 0) return 0;
    p += 6;
    p = skip_ws(p);
    return (*p == '>');
}

static int is_else_close(const char *line)
{
    const char *p = skip_ws(line);
    if (*p != '<' || p[1] != '/') return 0;
    p += 2;
    if (strncasecmp(p, "Else", 4) != 0) return 0;
    p += 4;
    /* Must NOT be ElseIf */
    if (p[0] && isalpha((unsigned char)p[0])) return 0;
    p = skip_ws(p);
    return (*p == '>');
}

/* ------------------------------------------------------------------ */
/*  Files block detection helpers                                      */
/* ------------------------------------------------------------------ */

/**
 * Check if a line is a <Files filename> opening tag.
 * If so, extracts the filename into *out_filename (caller must free).
 * Returns 1 if matched, 0 otherwise.
 */
static int is_files_open(const char *line, char **out_filename)
{
    const char *p = skip_ws(line);
    if (*p != '<')
        return 0;
    p++;
    const char *after = match_kw(p, "Files");
    if (!after)
        return 0;

    /* Ensure we don't match "FilesMatch" */
    if (strncasecmp(p, "FilesMatch", 10) == 0)
        return 0;

    p = skip_ws(after);

    /* Extract filename (may be quoted) */
    if (*p == '"') {
        p++;
        const char *end = strchr(p, '"');
        if (!end)
            return 0;
        *out_filename = strndup(p, (size_t)(end - p));
        /* Verify closing '>' follows */
        p = skip_ws(end + 1);
        if (*p != '>') {
            free(*out_filename);
            *out_filename = NULL;
            return 0;
        }
        return 1;
    }

    /* Unquoted filename */
    const char *start = p;
    while (*p && *p != '>' && !isspace((unsigned char)*p))
        p++;
    if (p == start)
        return 0;
    *out_filename = strndup(start, (size_t)(p - start));
    p = skip_ws(p);
    if (*p != '>') {
        free(*out_filename);
        *out_filename = NULL;
        return 0;
    }
    return 1;
}

/**
 * Check if a line is a </Files> closing tag.
 * Accepts both </Files> and </Files > (with optional space).
 * Does NOT match </FilesMatch>.
 */
static int is_files_close(const char *line)
{
    const char *p = skip_ws(line);
    if (*p != '<')
        return 0;
    p++;
    if (*p != '/')
        return 0;
    p++;

    size_t kw_len = strlen("Files");
    if (strncasecmp(p, "Files", kw_len) != 0)
        return 0;
    p += kw_len;

    /* Make sure we don't match </FilesMatch> */
    if (*p && *p != '>' && !isspace((unsigned char)*p))
        return 0;

    p = skip_ws(p);
    return (*p == '>');
}

/* ------------------------------------------------------------------ */
/*  RequireAny / RequireAll block detection helpers                     */
/* ------------------------------------------------------------------ */

static int is_require_any_open(const char *line)
{
    const char *p = skip_ws(line);
    if (*p != '<') return 0;
    p = skip_ws(p + 1);
    if (strncasecmp(p, "RequireAny", 10) != 0) return 0;
    p += 10;
    p = skip_ws(p);
    return (*p == '>');
}

static int is_require_any_close(const char *line)
{
    const char *p = skip_ws(line);
    if (*p != '<') return 0;
    p++;
    if (*p != '/') return 0;
    p++;
    if (strncasecmp(p, "RequireAny", 10) != 0) return 0;
    p += 10;
    p = skip_ws(p);
    return (*p == '>');
}

static int is_require_all_open(const char *line)
{
    const char *p = skip_ws(line);
    if (*p != '<') return 0;
    p = skip_ws(p + 1);
    if (strncasecmp(p, "RequireAll", 10) != 0) return 0;
    p += 10;
    p = skip_ws(p);
    return (*p == '>');
}

static int is_require_all_close(const char *line)
{
    const char *p = skip_ws(line);
    if (*p != '<') return 0;
    p++;
    if (*p != '/') return 0;
    p++;
    if (strncasecmp(p, "RequireAll", 10) != 0) return 0;
    p += 10;
    p = skip_ws(p);
    return (*p == '>');
}

/* ------------------------------------------------------------------ */
/*  Limit / LimitExcept block detection helpers                        */
/* ------------------------------------------------------------------ */

static int is_limit_open(const char *line, char **out_methods)
{
    const char *p = skip_ws(line);
    if (*p != '<') return 0;
    p = skip_ws(p + 1);
    /* Must match "Limit" but NOT "LimitExcept" */
    if (strncasecmp(p, "LimitExcept", 11) == 0)
        return 0;
    if (strncasecmp(p, "Limit", 5) != 0) return 0;
    p += 5;
    if (!isspace((unsigned char)*p)) return 0;
    p = skip_ws(p);
    /* Extract methods up to '>' */
    const char *start = p;
    while (*p && *p != '>') p++;
    if (*p != '>') return 0;
    /* Trim trailing whitespace from methods */
    const char *end = p;
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    if (end <= start) return 0;
    *out_methods = strndup(start, (size_t)(end - start));
    return 1;
}

static int is_limit_close(const char *line)
{
    const char *p = skip_ws(line);
    if (*p != '<') return 0;
    p++;
    if (*p != '/') return 0;
    p++;
    /* Must match "Limit" but NOT "LimitExcept" */
    if (strncasecmp(p, "LimitExcept", 11) == 0)
        return 0;
    if (strncasecmp(p, "Limit", 5) != 0) return 0;
    p += 5;
    p = skip_ws(p);
    return (*p == '>');
}

static int is_limit_except_open(const char *line, char **out_methods)
{
    const char *p = skip_ws(line);
    if (*p != '<') return 0;
    p = skip_ws(p + 1);
    if (strncasecmp(p, "LimitExcept", 11) != 0) return 0;
    p += 11;
    if (!isspace((unsigned char)*p)) return 0;
    p = skip_ws(p);
    const char *start = p;
    while (*p && *p != '>') p++;
    if (*p != '>') return 0;
    const char *end = p;
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    if (end <= start) return 0;
    *out_methods = strndup(start, (size_t)(end - start));
    return 1;
}

static int is_limit_except_close(const char *line)
{
    const char *p = skip_ws(line);
    if (*p != '<') return 0;
    p++;
    if (*p != '/') return 0;
    p++;
    if (strncasecmp(p, "LimitExcept", 11) != 0) return 0;
    p += 11;
    p = skip_ws(p);
    return (*p == '>');
}

/* ------------------------------------------------------------------ */
/*  Main parser entry point                                            */
/* ------------------------------------------------------------------ */

/**
 * Maximum nesting depth for IfModule blocks.
 */
#define MAX_IFMODULE_DEPTH 16

htaccess_directive_t *htaccess_parse(const char *content, size_t len,
                                     const char *filepath)
{
    if (!content || len == 0)
        return NULL;

    /* Make a NUL-terminated copy for safe string operations */
    char *buf = (char *)malloc(len + 1);
    if (!buf)
        return NULL;
    memcpy(buf, content, len);
    buf[len] = '\0';

    htaccess_directive_t *head = NULL;
    htaccess_directive_t *tail = NULL;

    const char *fp = filepath ? filepath : "<unknown>";
    int line_num = 0;
    int in_files_match = 0;
    char *fm_pattern = NULL;
    int fm_start_line = 0;
    htaccess_directive_t *fm_children_head = NULL;
    htaccess_directive_t *fm_children_tail = NULL;

    /* Files block state */
    int in_files = 0;
    char *files_name = NULL;
    int files_start_line = 0;
    htaccess_directive_t *files_children_head = NULL;
    htaccess_directive_t *files_children_tail = NULL;

    /* RequireAny block state */
    int in_require_any = 0;
    int require_any_start_line = 0;
    htaccess_directive_t *rqa_children_head = NULL;
    htaccess_directive_t *rqa_children_tail = NULL;

    /* RequireAll block state */
    int in_require_all = 0;
    int require_all_start_line = 0;
    htaccess_directive_t *rqall_children_head = NULL;
    htaccess_directive_t *rqall_children_tail = NULL;

    /* Limit block state */
    int in_limit = 0;
    char *limit_methods = NULL;
    int limit_start_line = 0;
    directive_type_t limit_type = DIR_LIMIT;
    htaccess_directive_t *limit_children_head = NULL;
    htaccess_directive_t *limit_children_tail = NULL;

    /* IfModule nesting stack */
    int ifmod_depth = 0;
    char *ifmod_names[MAX_IFMODULE_DEPTH];
    int   ifmod_negated[MAX_IFMODULE_DEPTH];
    int   ifmod_start_lines[MAX_IFMODULE_DEPTH];
    htaccess_directive_t *ifmod_children_head[MAX_IFMODULE_DEPTH];
    htaccess_directive_t *ifmod_children_tail[MAX_IFMODULE_DEPTH];
    /* State snapshots at IfModule entry — to avoid cleaning outer containers */
    int ifmod_saved_if_depth[MAX_IFMODULE_DEPTH];
    int ifmod_saved_in_files[MAX_IFMODULE_DEPTH];
    int ifmod_saved_in_files_match[MAX_IFMODULE_DEPTH];
    int ifmod_saved_in_require_any[MAX_IFMODULE_DEPTH];
    int ifmod_saved_in_require_all[MAX_IFMODULE_DEPTH];
    int ifmod_saved_in_limit[MAX_IFMODULE_DEPTH];

    /* If/ElseIf/Else nesting stack */
#define MAX_IF_DEPTH 16
    int if_depth = 0;
    int   if_types[MAX_IF_DEPTH];       /* DIR_IF, DIR_ELSEIF, DIR_ELSE */
    char *if_exprs[MAX_IF_DEPTH];       /* expression text (NULL for Else) */
    int   if_start_lines[MAX_IF_DEPTH];
    htaccess_directive_t *if_children_head[MAX_IF_DEPTH];
    htaccess_directive_t *if_children_tail[MAX_IF_DEPTH];

    /* RewriteCond pending chain — conditions accumulate until a RewriteRule
     * is encountered, then they are attached to that rule. */
    htaccess_directive_t *pending_conds = NULL;
    htaccess_directive_t *pending_conds_tail = NULL;

    /* Skip counter for orphaned ElseIf/Else blocks — when > 0, all lines
     * are discarded until the matching close tag decrements it to 0. */
    int skip_if_depth = 0;

    /* Manual line splitting to correctly count empty lines */
    char *cur = buf;
    while (cur && *cur) {
        line_num++;

        /* Find end of current line */
        char *eol = strchr(cur, '\n');
        if (eol)
            *eol = '\0';

        /* Trim trailing \r (Windows line endings) and whitespace */
        {
            size_t llen = strlen(cur);
            while (llen > 0 && (cur[llen - 1] == '\r' ||
                                isspace((unsigned char)cur[llen - 1])))
                cur[--llen] = '\0';
        }

        /* Trim leading whitespace */
        const char *p = skip_ws(cur);

        /* Advance to next line */
        cur = eol ? eol + 1 : NULL;

        /* Skip empty lines and comments */
        if (*p == '\0' || *p == '#')
            continue;

        /* If we're skipping an orphaned ElseIf/Else block, consume lines
         * until the matching close tag is found. */
        if (skip_if_depth > 0) {
            if (is_if_open(p, NULL) || is_elseif_open(p, NULL) || is_else_open(p))
                skip_if_depth++;
            else if (is_if_close(p) || is_elseif_close(p) || is_else_close(p))
                skip_if_depth--;
            continue;
        }

        /* --- IfModule close tag --- */
        if (ifmod_depth > 0 && is_ifmodule_close(p)) {
            int idx = ifmod_depth - 1;

            /* Only clean containers opened INSIDE this IfModule (not outer ones) */
            if (in_files_match && !ifmod_saved_in_files_match[idx]) {
                lsi_log(NULL, LSI_LOG_WARN,
                        "[htaccess] %s:%d: unclosed <FilesMatch> block inside <IfModule>, discarding",
                        fp, fm_start_line);
                free(fm_pattern);
                htaccess_directives_free(fm_children_head);
                fm_pattern = NULL;
                fm_children_head = NULL;
                fm_children_tail = NULL;
                in_files_match = 0;
            }

            if (in_files && !ifmod_saved_in_files[idx]) {
                lsi_log(NULL, LSI_LOG_WARN,
                        "[htaccess] %s:%d: unclosed <Files> block inside <IfModule>, discarding",
                        fp, files_start_line);
                free(files_name);
                htaccess_directives_free(files_children_head);
                files_name = NULL;
                files_children_head = NULL;
                files_children_tail = NULL;
                in_files = 0;
            }

            /* Discard orphaned RewriteCond inside this IfModule */
            if (pending_conds) {
                lsi_log(NULL, LSI_LOG_WARN,
                        "[htaccess] %s:%d: orphaned RewriteCond inside <IfModule>, discarding",
                        fp, line_num);
                htaccess_directives_free(pending_conds);
                pending_conds = NULL;
                pending_conds_tail = NULL;
            }

            if (in_require_any && !ifmod_saved_in_require_any[idx]) {
                lsi_log(NULL, LSI_LOG_WARN,
                        "[htaccess] %s:%d: unclosed <RequireAny> inside <IfModule>, discarding",
                        fp, require_any_start_line);
                htaccess_directives_free(rqa_children_head);
                rqa_children_head = NULL; rqa_children_tail = NULL;
                in_require_any = 0;
            }
            if (in_require_all && !ifmod_saved_in_require_all[idx]) {
                lsi_log(NULL, LSI_LOG_WARN,
                        "[htaccess] %s:%d: unclosed <RequireAll> inside <IfModule>, discarding",
                        fp, require_all_start_line);
                htaccess_directives_free(rqall_children_head);
                rqall_children_head = NULL; rqall_children_tail = NULL;
                in_require_all = 0;
            }
            if (in_limit && !ifmod_saved_in_limit[idx]) {
                lsi_log(NULL, LSI_LOG_WARN,
                        "[htaccess] %s:%d: unclosed <Limit> inside <IfModule>, discarding",
                        fp, limit_start_line);
                free(limit_methods);
                htaccess_directives_free(limit_children_head);
                limit_methods = NULL; limit_children_head = NULL; limit_children_tail = NULL;
                in_limit = 0;
            }
            while (if_depth > ifmod_saved_if_depth[idx]) {
                if_depth--;
                lsi_log(NULL, LSI_LOG_WARN,
                        "[htaccess] %s:%d: unclosed <If> inside <IfModule>, discarding",
                        fp, if_start_lines[if_depth]);
                free(if_exprs[if_depth]);
                htaccess_directives_free(if_children_head[if_depth]);
            }

            /* Build the IfModule container node */
            htaccess_directive_t *im = alloc_directive(DIR_IFMODULE, ifmod_start_lines[idx]);
            if (im) {
                im->name = ifmod_names[idx];
                im->data.ifmodule.negated = ifmod_negated[idx];
                im->data.ifmodule.children = ifmod_children_head[idx];

                ifmod_depth--;

                /* Append to parent context — full container chain */
                if (if_depth > 0) {
                    int pidx = if_depth - 1;
                    append_directive(&if_children_head[pidx],
                                     &if_children_tail[pidx], im);
                } else if (in_files_match)
                    append_directive(&fm_children_head, &fm_children_tail, im);
                else if (in_files)
                    append_directive(&files_children_head, &files_children_tail, im);
                else if (in_require_any)
                    append_directive(&rqa_children_head, &rqa_children_tail, im);
                else if (in_require_all)
                    append_directive(&rqall_children_head, &rqall_children_tail, im);
                else if (in_limit)
                    append_directive(&limit_children_head, &limit_children_tail, im);
                else if (ifmod_depth > 0) {
                    int parent = ifmod_depth - 1;
                    append_directive(&ifmod_children_head[parent],
                                     &ifmod_children_tail[parent], im);
                } else {
                    append_directive(&head, &tail, im);
                }
            } else {
                free(ifmod_names[idx]);
                htaccess_directives_free(ifmod_children_head[idx]);
                ifmod_depth--;
            }
            continue;
        }

        /* --- IfModule open tag --- */
        {
            char *mod_name = NULL;
            int neg = 0;
            if (is_ifmodule_open(p, &mod_name, &neg)) {
                if (ifmod_depth >= MAX_IFMODULE_DEPTH) {
                    lsi_log(NULL, LSI_LOG_WARN,
                            "[htaccess] %s:%d: IfModule nesting too deep, skipping",
                            fp, line_num);
                    free(mod_name);
                } else {
                    int idx = ifmod_depth;
                    ifmod_names[idx] = mod_name;
                    ifmod_negated[idx] = neg;
                    ifmod_start_lines[idx] = line_num;
                    ifmod_children_head[idx] = NULL;
                    ifmod_children_tail[idx] = NULL;
                    /* Save outer container state snapshot */
                    ifmod_saved_if_depth[idx] = if_depth;
                    ifmod_saved_in_files[idx] = in_files;
                    ifmod_saved_in_files_match[idx] = in_files_match;
                    ifmod_saved_in_require_any[idx] = in_require_any;
                    ifmod_saved_in_require_all[idx] = in_require_all;
                    ifmod_saved_in_limit[idx] = in_limit;
                    ifmod_depth++;
                }
                continue;
            }
        }

        /* --- If/ElseIf/Else close tags --- */
        if (if_depth > 0 &&
            (is_if_close(p) || is_elseif_close(p) || is_else_close(p))) {
            int idx = if_depth - 1;

            /* Validate close tag matches the open tag type */
            int close_is_if = is_if_close(p);
            int close_is_elseif = is_elseif_close(p);
            int close_is_else = is_else_close(p);
            directive_type_t open_type = (directive_type_t)if_types[idx];
            int mismatch = 0;
            if (close_is_if && open_type != DIR_IF) mismatch = 1;
            else if (close_is_elseif && open_type != DIR_ELSEIF) mismatch = 1;
            else if (close_is_else && open_type != DIR_ELSE) mismatch = 1;
            if (mismatch) {
                lsi_log(NULL, LSI_LOG_WARN,
                        "[htaccess] %s:%d: close tag does not match open tag, discarding block",
                        fp, line_num);
                free(if_exprs[idx]);
                htaccess_directives_free(if_children_head[idx]);
                if_depth--;
                continue;
            }

            /* Create the directive node */
            directive_type_t dtype = (directive_type_t)if_types[idx];
            htaccess_directive_t *d = alloc_directive(dtype, if_start_lines[idx]);
            if (d) {
                if (if_exprs[idx] && dtype != DIR_ELSE) {
                    d->data.if_block.condition = parse_expr(if_exprs[idx]);
                } else {
                    d->data.if_block.condition = NULL;
                }
                d->data.if_block.children = if_children_head[idx];
                /* Append to the correct parent container.
                 * Check parent If FIRST (nested <If> inside <If>). */
                if (if_depth > 1) {
                    int pidx = if_depth - 2;
                    append_directive(&if_children_head[pidx],
                                     &if_children_tail[pidx], d);
                } else if (in_files_match)
                    append_directive(&fm_children_head, &fm_children_tail, d);
                else if (in_files)
                    append_directive(&files_children_head, &files_children_tail, d);
                else if (in_require_any)
                    append_directive(&rqa_children_head, &rqa_children_tail, d);
                else if (in_require_all)
                    append_directive(&rqall_children_head, &rqall_children_tail, d);
                else if (in_limit)
                    append_directive(&limit_children_head, &limit_children_tail, d);
                else if (ifmod_depth > 0) {
                    int pidx = ifmod_depth - 1;
                    append_directive(&ifmod_children_head[pidx],
                                     &ifmod_children_tail[pidx], d);
                } else
                    append_directive(&head, &tail, d);
            } else {
                htaccess_directives_free(if_children_head[idx]);
            }
            free(if_exprs[idx]);
            if_depth--;
            if (pending_conds) {
                lsi_log(NULL, LSI_LOG_WARN,
                        "[htaccess] %s:%d: orphaned RewriteCond inside <If>, discarding",
                        fp, line_num);
                htaccess_directives_free(pending_conds);
                pending_conds = NULL;
                pending_conds_tail = NULL;
            }
            continue;
        }

        /* --- If open tag --- */
        {
            char *expr_text = NULL;
            if (is_if_open(p, &expr_text)) {
                if (if_depth >= MAX_IF_DEPTH) {
                    lsi_log(NULL, LSI_LOG_WARN,
                            "[htaccess] %s:%d: If nesting too deep", fp, line_num);
                    free(expr_text);
                } else {
                    int idx = if_depth;
                    if_types[idx] = DIR_IF;
                    if_exprs[idx] = expr_text;
                    if_start_lines[idx] = line_num;
                    if_children_head[idx] = NULL;
                    if_children_tail[idx] = NULL;
                    if_depth++;
                }
                continue;
            }
        }

        /* --- ElseIf open tag --- */
        {
            char *expr_text = NULL;
            if (is_elseif_open(p, &expr_text)) {
                /* ElseIf must follow a closed If or ElseIf block */
                htaccess_directive_t *prev_sib = NULL;
                if (if_depth > 0)
                    prev_sib = if_children_tail[if_depth - 1];
                else if (in_files_match) prev_sib = fm_children_tail;
                else if (in_files) prev_sib = files_children_tail;
                else if (in_require_any) prev_sib = rqa_children_tail;
                else if (in_require_all) prev_sib = rqall_children_tail;
                else if (in_limit) prev_sib = limit_children_tail;
                else if (ifmod_depth > 0) prev_sib = ifmod_children_tail[ifmod_depth - 1];
                else prev_sib = tail;

                if (!prev_sib ||
                    (prev_sib->type != DIR_IF && prev_sib->type != DIR_ELSEIF)) {
                    lsi_log(NULL, LSI_LOG_WARN,
                            "[htaccess] %s:%d: <ElseIf> without preceding <If>, skipping",
                            fp, line_num);
                    free(expr_text);
                    skip_if_depth++;
                } else if (if_depth >= MAX_IF_DEPTH) {
                    free(expr_text);
                } else {
                    int idx = if_depth;
                    if_types[idx] = DIR_ELSEIF;
                    if_exprs[idx] = expr_text;
                    if_start_lines[idx] = line_num;
                    if_children_head[idx] = NULL;
                    if_children_tail[idx] = NULL;
                    if_depth++;
                }
                continue;
            }
        }

        /* --- Else open tag --- */
        if (is_else_open(p)) {
            /* Else must follow a closed If or ElseIf block */
            htaccess_directive_t *prev_sib = NULL;
            if (if_depth > 0)
                prev_sib = if_children_tail[if_depth - 1];
            else if (in_files_match) prev_sib = fm_children_tail;
            else if (in_files) prev_sib = files_children_tail;
            else if (in_require_any) prev_sib = rqa_children_tail;
            else if (in_require_all) prev_sib = rqall_children_tail;
            else if (in_limit) prev_sib = limit_children_tail;
            else if (ifmod_depth > 0) prev_sib = ifmod_children_tail[ifmod_depth - 1];
            else prev_sib = tail;

            if (!prev_sib ||
                (prev_sib->type != DIR_IF && prev_sib->type != DIR_ELSEIF)) {
                lsi_log(NULL, LSI_LOG_WARN,
                        "[htaccess] %s:%d: <Else> without preceding <If>, skipping",
                        fp, line_num);
                skip_if_depth++;
            } else if (if_depth >= MAX_IF_DEPTH) {
                /* skip */
            } else {
                int idx = if_depth;
                if_types[idx] = DIR_ELSE;
                if_exprs[idx] = NULL;
                if_start_lines[idx] = line_num;
                if_children_head[idx] = NULL;
                if_children_tail[idx] = NULL;
                if_depth++;
            }
            continue;
        }

        /* --- FilesMatch close tag --- */
        if (in_files_match && is_files_match_close(p)) {
            htaccess_directive_t *fm = alloc_directive(DIR_FILES_MATCH, fm_start_line);
            if (fm) {
                fm->data.files_match.pattern = fm_pattern;
                fm->data.files_match.children = fm_children_head;
                /* Append to current context — full container chain */
                if (if_depth > 0) {
                    int idx = if_depth - 1;
                    append_directive(&if_children_head[idx],
                                     &if_children_tail[idx], fm);
                } else if (in_require_any)
                    append_directive(&rqa_children_head, &rqa_children_tail, fm);
                else if (in_require_all)
                    append_directive(&rqall_children_head, &rqall_children_tail, fm);
                else if (in_limit)
                    append_directive(&limit_children_head, &limit_children_tail, fm);
                else if (ifmod_depth > 0) {
                    int idx = ifmod_depth - 1;
                    append_directive(&ifmod_children_head[idx],
                                     &ifmod_children_tail[idx], fm);
                } else {
                    append_directive(&head, &tail, fm);
                }
            } else {
                free(fm_pattern);
                htaccess_directives_free(fm_children_head);
            }
            fm_pattern = NULL;
            fm_children_head = NULL;
            fm_children_tail = NULL;
            in_files_match = 0;
            /* Discard orphaned RewriteCond inside this container */
            if (pending_conds) {
                lsi_log(NULL, LSI_LOG_WARN,
                        "[htaccess] %s:%d: orphaned RewriteCond inside <FilesMatch>, discarding",
                        fp, line_num);
                htaccess_directives_free(pending_conds);
                pending_conds = NULL;
                pending_conds_tail = NULL;
            }
            continue;
        }

        /* --- FilesMatch open tag --- */
        if (!in_files_match && !in_files) {
            char *pattern = NULL;
            if (is_files_match_open(p, &pattern)) {
                in_files_match = 1;
                fm_pattern = pattern;
                fm_start_line = line_num;
                fm_children_head = NULL;
                fm_children_tail = NULL;
                continue;
            }
        }

        /* --- Files close tag --- */
        if (in_files && is_files_close(p)) {
            htaccess_directive_t *fd = alloc_directive(DIR_FILES, files_start_line);
            if (fd) {
                fd->name = files_name;
                fd->data.files.children = files_children_head;
                /* Append to current context — full container chain */
                if (if_depth > 0) {
                    int idx = if_depth - 1;
                    append_directive(&if_children_head[idx],
                                     &if_children_tail[idx], fd);
                } else if (in_require_any)
                    append_directive(&rqa_children_head, &rqa_children_tail, fd);
                else if (in_require_all)
                    append_directive(&rqall_children_head, &rqall_children_tail, fd);
                else if (in_limit)
                    append_directive(&limit_children_head, &limit_children_tail, fd);
                else if (ifmod_depth > 0) {
                    int idx = ifmod_depth - 1;
                    append_directive(&ifmod_children_head[idx],
                                     &ifmod_children_tail[idx], fd);
                } else {
                    append_directive(&head, &tail, fd);
                }
            } else {
                free(files_name);
                htaccess_directives_free(files_children_head);
            }
            files_name = NULL;
            files_children_head = NULL;
            files_children_tail = NULL;
            in_files = 0;
            if (pending_conds) {
                lsi_log(NULL, LSI_LOG_WARN,
                        "[htaccess] %s:%d: orphaned RewriteCond inside <Files>, discarding",
                        fp, line_num);
                htaccess_directives_free(pending_conds);
                pending_conds = NULL;
                pending_conds_tail = NULL;
            }
            continue;
        }

        /* --- Files open tag --- */
        if (!in_files_match && !in_files) {
            char *fname = NULL;
            if (is_files_open(p, &fname)) {
                in_files = 1;
                files_name = fname;
                files_start_line = line_num;
                files_children_head = NULL;
                files_children_tail = NULL;
                continue;
            }
        }

        /* --- RequireAny close tag --- */
        if (in_require_any && is_require_any_close(p)) {
            htaccess_directive_t *rqa = alloc_directive(DIR_REQUIRE_ANY_OPEN, require_any_start_line);
            if (rqa) {
                rqa->data.require_container.children = rqa_children_head;
                /* Append to nearest enclosing container — check If first */
                if (if_depth > 0) {
                    int idx = if_depth - 1;
                    append_directive(&if_children_head[idx],
                                     &if_children_tail[idx], rqa);
                } else if (in_require_all) {
                    append_directive(&rqall_children_head, &rqall_children_tail, rqa);
                } else if (in_files) {
                    append_directive(&files_children_head, &files_children_tail, rqa);
                } else if (in_files_match) {
                    append_directive(&fm_children_head, &fm_children_tail, rqa);
                } else if (in_limit) {
                    append_directive(&limit_children_head, &limit_children_tail, rqa);
                } else if (ifmod_depth > 0) {
                    int idx = ifmod_depth - 1;
                    append_directive(&ifmod_children_head[idx],
                                     &ifmod_children_tail[idx], rqa);
                } else {
                    append_directive(&head, &tail, rqa);
                }
            } else {
                htaccess_directives_free(rqa_children_head);
            }
            rqa_children_head = NULL;
            rqa_children_tail = NULL;
            in_require_any = 0;
            if (pending_conds) {
                lsi_log(NULL, LSI_LOG_WARN,
                        "[htaccess] %s:%d: orphaned RewriteCond inside <RequireAny>, discarding",
                        fp, line_num);
                htaccess_directives_free(pending_conds);
                pending_conds = NULL;
                pending_conds_tail = NULL;
            }
            continue;
        }

        /* --- RequireAny open tag ---
         * Allow inside RequireAll (single-level nesting) */
        if (!in_require_any && is_require_any_open(p)) {
            in_require_any = 1;
            require_any_start_line = line_num;
            rqa_children_head = NULL;
            rqa_children_tail = NULL;
            continue;
        }

        /* --- RequireAll close tag --- */
        if (in_require_all && is_require_all_close(p)) {
            htaccess_directive_t *rqall = alloc_directive(DIR_REQUIRE_ALL_OPEN, require_all_start_line);
            if (rqall) {
                rqall->data.require_container.children = rqall_children_head;
                /* Append to nearest enclosing container — check If first */
                if (if_depth > 0) {
                    int idx = if_depth - 1;
                    append_directive(&if_children_head[idx],
                                     &if_children_tail[idx], rqall);
                } else if (in_require_any) {
                    append_directive(&rqa_children_head, &rqa_children_tail, rqall);
                } else if (in_files) {
                    append_directive(&files_children_head, &files_children_tail, rqall);
                } else if (in_files_match) {
                    append_directive(&fm_children_head, &fm_children_tail, rqall);
                } else if (in_limit) {
                    append_directive(&limit_children_head, &limit_children_tail, rqall);
                } else if (ifmod_depth > 0) {
                    int idx = ifmod_depth - 1;
                    append_directive(&ifmod_children_head[idx],
                                     &ifmod_children_tail[idx], rqall);
                } else {
                    append_directive(&head, &tail, rqall);
                }
            } else {
                htaccess_directives_free(rqall_children_head);
            }
            rqall_children_head = NULL;
            rqall_children_tail = NULL;
            in_require_all = 0;
            if (pending_conds) {
                lsi_log(NULL, LSI_LOG_WARN,
                        "[htaccess] %s:%d: orphaned RewriteCond inside <RequireAll>, discarding",
                        fp, line_num);
                htaccess_directives_free(pending_conds);
                pending_conds = NULL;
                pending_conds_tail = NULL;
            }
            continue;
        }

        /* --- RequireAll open tag ---
         * Allow inside RequireAny (single-level nesting) */
        if (!in_require_all && is_require_all_open(p)) {
            in_require_all = 1;
            require_all_start_line = line_num;
            rqall_children_head = NULL;
            rqall_children_tail = NULL;
            continue;
        }

        /* --- Limit close tag --- */
        if (in_limit && !in_require_any && !in_require_all) {
            int is_close = 0;
            if (limit_type == DIR_LIMIT)
                is_close = is_limit_close(p);
            else
                is_close = is_limit_except_close(p);

            if (is_close) {
                htaccess_directive_t *ld = alloc_directive(limit_type, limit_start_line);
                if (ld) {
                    ld->data.limit.methods = limit_methods;
                    ld->data.limit.children = limit_children_head;
                    if (if_depth > 0) {
                        int idx = if_depth - 1;
                        append_directive(&if_children_head[idx],
                                         &if_children_tail[idx], ld);
                    } else if (in_files_match)
                        append_directive(&fm_children_head, &fm_children_tail, ld);
                    else if (in_files)
                        append_directive(&files_children_head, &files_children_tail, ld);
                    else if (ifmod_depth > 0) {
                        int idx = ifmod_depth - 1;
                        append_directive(&ifmod_children_head[idx],
                                         &ifmod_children_tail[idx], ld);
                    } else {
                        append_directive(&head, &tail, ld);
                    }
                } else {
                    free(limit_methods);
                    htaccess_directives_free(limit_children_head);
                }
                limit_methods = NULL;
                limit_children_head = NULL;
                limit_children_tail = NULL;
                in_limit = 0;
                if (pending_conds) {
                    lsi_log(NULL, LSI_LOG_WARN,
                            "[htaccess] %s:%d: orphaned RewriteCond inside <Limit>, discarding",
                            fp, line_num);
                    htaccess_directives_free(pending_conds);
                    pending_conds = NULL;
                    pending_conds_tail = NULL;
                }
                continue;
            }
        }

        /* --- LimitExcept open tag (must check before Limit) --- */
        if (!in_limit && !in_require_any && !in_require_all) {
            char *methods = NULL;
            if (is_limit_except_open(p, &methods)) {
                in_limit = 1;
                limit_type = DIR_LIMIT_EXCEPT;
                limit_methods = methods;
                limit_start_line = line_num;
                limit_children_head = NULL;
                limit_children_tail = NULL;
                continue;
            }
        }

        /* --- Limit open tag --- */
        if (!in_limit && !in_require_any && !in_require_all) {
            char *methods = NULL;
            if (is_limit_open(p, &methods)) {
                in_limit = 1;
                limit_type = DIR_LIMIT;
                limit_methods = methods;
                limit_start_line = line_num;
                limit_children_head = NULL;
                limit_children_tail = NULL;
                continue;
            }
        }

        /* Parse the directive line */
        htaccess_directive_t *dir = parse_line(p, line_num);
        if (dir) {
            /* RewriteCond → RewriteRule linking:
             * RewriteCond lines accumulate in pending_conds until a
             * RewriteRule is encountered. Then all pending conditions
             * are attached to that rule's conditions chain. */
            if (dir->type == DIR_REWRITE_COND) {
                dir->next = NULL;
                if (!pending_conds)
                    pending_conds = dir;
                else
                    pending_conds_tail->next = dir;
                pending_conds_tail = dir;
                continue; /* Don't add to main list */
            }
            if (dir->type == DIR_REWRITE_RULE && pending_conds) {
                dir->data.rewrite_rule.conditions = pending_conds;
                pending_conds = NULL;
                pending_conds_tail = NULL;
            }

            /* Note: pending_conds are NOT discarded when non-rewrite directives
             * appear. Apache allows other directives between RewriteCond and
             * RewriteRule (e.g., RewriteBase between conditions and rule).
             * Orphaned conds are only cleaned up at container boundaries
             * (IfModule close) and at end-of-file. */

            /* If block is always the innermost container when active.
             * Must be checked FIRST so <If> inside <Files>/<IfModule> works. */
            if (if_depth > 0) {
                int idx = if_depth - 1;
                append_directive(&if_children_head[idx],
                                 &if_children_tail[idx], dir);
            } else if (in_files_match) {
                append_directive(&fm_children_head, &fm_children_tail, dir);
            } else if (in_files) {
                append_directive(&files_children_head, &files_children_tail, dir);
            } else if (in_require_any) {
                /* Only allow Require* directives inside RequireAny */
                if (dir->type == DIR_REQUIRE_ALL_GRANTED ||
                    dir->type == DIR_REQUIRE_ALL_DENIED ||
                    dir->type == DIR_REQUIRE_IP ||
                    dir->type == DIR_REQUIRE_NOT_IP ||
                    dir->type == DIR_REQUIRE_ENV ||
                    dir->type == DIR_REQUIRE_VALID_USER ||
                    dir->type == DIR_REQUIRE_ANY_OPEN ||
                    dir->type == DIR_REQUIRE_ALL_OPEN) {
                    append_directive(&rqa_children_head, &rqa_children_tail, dir);
                } else {
                    lsi_log(NULL, LSI_LOG_WARN,
                            "[htaccess] %s:%d: non-Require directive inside <RequireAny>, ignoring",
                            fp, line_num);
                    htaccess_directives_free(dir);
                }
            } else if (in_require_all) {
                /* Only allow Require* directives inside RequireAll */
                if (dir->type == DIR_REQUIRE_ALL_GRANTED ||
                    dir->type == DIR_REQUIRE_ALL_DENIED ||
                    dir->type == DIR_REQUIRE_IP ||
                    dir->type == DIR_REQUIRE_NOT_IP ||
                    dir->type == DIR_REQUIRE_ENV ||
                    dir->type == DIR_REQUIRE_VALID_USER ||
                    dir->type == DIR_REQUIRE_ANY_OPEN ||
                    dir->type == DIR_REQUIRE_ALL_OPEN) {
                    append_directive(&rqall_children_head, &rqall_children_tail, dir);
                } else {
                    lsi_log(NULL, LSI_LOG_WARN,
                            "[htaccess] %s:%d: non-Require directive inside <RequireAll>, ignoring",
                            fp, line_num);
                    htaccess_directives_free(dir);
                }
            } else if (in_limit) {
                append_directive(&limit_children_head, &limit_children_tail, dir);
            } else if (ifmod_depth > 0) {
                int idx = ifmod_depth - 1;
                append_directive(&ifmod_children_head[idx],
                                 &ifmod_children_tail[idx], dir);
            } else {
                append_directive(&head, &tail, dir);
            }
        } else {
            lsi_log(NULL, LSI_LOG_WARN,
                    "[htaccess] %s:%d: syntax error, skipping line: %s",
                    fp, line_num, p);
        }
    }

    /* Handle unclosed FilesMatch block */
    if (in_files_match) {
        lsi_log(NULL, LSI_LOG_WARN,
                "[htaccess] %s:%d: unclosed <FilesMatch> block, discarding",
                fp, fm_start_line);
        free(fm_pattern);
        htaccess_directives_free(fm_children_head);
    }

    /* Handle unclosed Files block */
    if (in_files) {
        lsi_log(NULL, LSI_LOG_WARN,
                "[htaccess] %s:%d: unclosed <Files> block, discarding",
                fp, files_start_line);
        free(files_name);
        htaccess_directives_free(files_children_head);
    }

    /* Handle unclosed RequireAny block */
    if (in_require_any) {
        lsi_log(NULL, LSI_LOG_WARN,
                "[htaccess] %s:%d: unclosed <RequireAny> block, discarding",
                fp, require_any_start_line);
        htaccess_directives_free(rqa_children_head);
    }

    /* Handle unclosed RequireAll block */
    if (in_require_all) {
        lsi_log(NULL, LSI_LOG_WARN,
                "[htaccess] %s:%d: unclosed <RequireAll> block, discarding",
                fp, require_all_start_line);
        htaccess_directives_free(rqall_children_head);
    }

    /* Handle unclosed Limit/LimitExcept block */
    if (in_limit) {
        lsi_log(NULL, LSI_LOG_WARN,
                "[htaccess] %s:%d: unclosed <%s> block, discarding",
                fp, limit_start_line,
                limit_type == DIR_LIMIT ? "Limit" : "LimitExcept");
        free(limit_methods);
        htaccess_directives_free(limit_children_head);
    }

    /* Handle unclosed IfModule blocks */
    while (ifmod_depth > 0) {
        ifmod_depth--;
        lsi_log(NULL, LSI_LOG_WARN,
                "[htaccess] %s:%d: unclosed <IfModule> block, discarding",
                fp, ifmod_start_lines[ifmod_depth]);
        free(ifmod_names[ifmod_depth]);
        htaccess_directives_free(ifmod_children_head[ifmod_depth]);
    }

    /* Handle unclosed If/ElseIf/Else blocks */
    while (if_depth > 0) {
        if_depth--;
        lsi_log(NULL, LSI_LOG_WARN,
                "[htaccess] %s:%d: unclosed <If> block, discarding",
                fp, if_start_lines[if_depth]);
        free(if_exprs[if_depth]);
        htaccess_directives_free(if_children_head[if_depth]);
    }

    /* Handle orphaned RewriteCond (no following RewriteRule) */
    if (pending_conds) {
        lsi_log(NULL, LSI_LOG_WARN,
                "[htaccess] %s: orphaned RewriteCond without RewriteRule, discarding",
                fp);
        htaccess_directives_free(pending_conds);
    }

    free(buf);
    return head;
}
