/**
 * htaccess_directive.h - Directive data model for OLS .htaccess module
 *
 * Defines the directive_type_t enum (71 directive types),
 * supporting enums (acl_order_t, bf_action_t), and the htaccess_directive_t
 * linked-list node structure with a union for type-specific fields.
 *
 * Validates: Requirements 2.2, 4.1-4.7, 5.1-5.4, 6.1-6.4, 7.1-7.4,
 *            8.1-8.3, 9.1, 10.1-10.4, 11.1-11.6, 12.1-12.8, 17.3
 */
#ifndef HTACCESS_DIRECTIVE_H
#define HTACCESS_DIRECTIVE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Directive type enumeration — covers all 71 supported .htaccess directives
 * (28 v1 + 43 v2).
 *
 * IMPORTANT: v1 values (0-27) MUST NOT be reordered or removed.
 * New v2 values are appended after DIR_BRUTE_FORCE_THROTTLE_DURATION
 * to preserve binary compatibility.
 */
typedef enum {
    /* === v1 existing types (0-27) — DO NOT MODIFY === */
    DIR_HEADER_SET,                    /* 0  */
    DIR_HEADER_UNSET,                  /* 1  */
    DIR_HEADER_APPEND,                 /* 2  */
    DIR_HEADER_MERGE,                  /* 3  */
    DIR_HEADER_ADD,                    /* 4  */
    DIR_REQUEST_HEADER_SET,            /* 5  */
    DIR_REQUEST_HEADER_UNSET,          /* 6  */
    DIR_PHP_VALUE,                     /* 7  */
    DIR_PHP_FLAG,                      /* 8  */
    DIR_PHP_ADMIN_VALUE,               /* 9  */
    DIR_PHP_ADMIN_FLAG,                /* 10 */
    DIR_ORDER,                         /* 11 */
    DIR_ALLOW_FROM,                    /* 12 */
    DIR_DENY_FROM,                     /* 13 */
    DIR_REDIRECT,                      /* 14 */
    DIR_REDIRECT_MATCH,                /* 15 */
    DIR_ERROR_DOCUMENT,                /* 16 */
    DIR_FILES_MATCH,                   /* 17 */
    DIR_EXPIRES_ACTIVE,                /* 18 */
    DIR_EXPIRES_BY_TYPE,               /* 19 */
    DIR_SETENV,                        /* 20 */
    DIR_SETENVIF,                      /* 21 */
    DIR_BROWSER_MATCH,                 /* 22 */
    DIR_BRUTE_FORCE_PROTECTION,        /* 23 */
    DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS,  /* 24 */
    DIR_BRUTE_FORCE_WINDOW,            /* 25 */
    DIR_BRUTE_FORCE_ACTION,            /* 26 */
    DIR_BRUTE_FORCE_THROTTLE_DURATION, /* 27 */

    /* === v2 new types (28+) — APPEND ONLY === */

    /* P1: Panel core directives */
    DIR_IFMODULE,                      /* 28 — container, children list */
    DIR_OPTIONS,                       /* 29 */
    DIR_FILES,                         /* 30 — container, children list */

    /* P2: Advanced directives */
    DIR_HEADER_ALWAYS_SET,             /* 31 */
    DIR_HEADER_ALWAYS_UNSET,           /* 32 */
    DIR_HEADER_ALWAYS_APPEND,          /* 33 */
    DIR_HEADER_ALWAYS_MERGE,           /* 34 */
    DIR_HEADER_ALWAYS_ADD,             /* 35 */
    DIR_EXPIRES_DEFAULT,               /* 36 */
    DIR_REQUIRE_ALL_GRANTED,           /* 37 */
    DIR_REQUIRE_ALL_DENIED,            /* 38 */
    DIR_REQUIRE_IP,                    /* 39 */
    DIR_REQUIRE_NOT_IP,                /* 40 */
    DIR_REQUIRE_ANY_OPEN,              /* 41 — container */
    DIR_REQUIRE_ALL_OPEN,              /* 42 — container */
    DIR_LIMIT,                         /* 43 — container */
    DIR_LIMIT_EXCEPT,                  /* 44 — container */

    /* P3: Auth/Handler directives */
    DIR_AUTH_TYPE,                      /* 45 */
    DIR_AUTH_NAME,                      /* 46 */
    DIR_AUTH_USER_FILE,                 /* 47 */
    DIR_REQUIRE_VALID_USER,            /* 48 */
    DIR_ADD_HANDLER,                   /* 49 */
    DIR_SET_HANDLER,                   /* 50 */
    DIR_ADD_TYPE,                      /* 51 */
    DIR_DIRECTORY_INDEX,               /* 52 */

    /* P4: Low priority directives */
    DIR_FORCE_TYPE,                    /* 53 */
    DIR_ADD_ENCODING,                  /* 54 */
    DIR_ADD_CHARSET,                   /* 55 */

    /* Brute force enhancements */
    DIR_BRUTE_FORCE_X_FORWARDED_FOR,   /* 56 */
    DIR_BRUTE_FORCE_WHITELIST,         /* 57 */
    DIR_BRUTE_FORCE_PROTECT_PATH,      /* 58 */

    /* P5: CyberPanel-inspired enhancements */
    DIR_SETENVIF_NOCASE,               /* 59 — case-insensitive SetEnvIf */

    /* P6: Header edit and advanced conditions */
    DIR_HEADER_EDIT,                   /* 60 — Header edit Name regex replacement */
    DIR_HEADER_EDIT_STAR,              /* 61 — Header edit* (global replace) */
    DIR_HEADER_ALWAYS_EDIT,            /* 62 — Header always edit */
    DIR_HEADER_ALWAYS_EDIT_STAR,       /* 63 — Header always edit* */

    /* P7: Rewrite directives (OLS patch required for execution) */
    DIR_REWRITE_ENGINE,                /* 64 — RewriteEngine On/Off */
    DIR_REWRITE_BASE,                  /* 65 — RewriteBase /path/ */
    DIR_REWRITE_COND,                  /* 66 — RewriteCond %{VAR} pattern [flags] */
    DIR_REWRITE_RULE,                  /* 67 — RewriteRule pattern target [flags] */

    /* P8: Phase 1 directive additions */
    DIR_ADD_DEFAULT_CHARSET,           /* 68 — AddDefaultCharset UTF-8|Off */
    DIR_DEFAULT_TYPE,                  /* 69 — DefaultType text/html */
    DIR_SATISFY,                       /* 70 — Satisfy Any|All */

    /* Phase 7b: RewriteEngine enhancements */
    DIR_REWRITE_OPTIONS,               /* 71 — RewriteOptions inherit|IgnoreInherit */
    DIR_REWRITE_MAP,                   /* 72 — RewriteMap name type:source */

    /* Phase 7b: Expression / conditional blocks */
    DIR_IF,                            /* 73 — <If "expression"> */
    DIR_ELSEIF,                        /* 74 — <ElseIf "expression"> */
    DIR_ELSE,                          /* 75 — <Else> */

    /* Phase 8: Additional Require types */
    DIR_REQUIRE_ENV,                   /* 76 — Require env VARNAME */

    /* Phase 9: HttpMime additions */
    DIR_REMOVE_TYPE,                   /* 77 — RemoveType .ext1 .ext2 */
    DIR_REMOVE_HANDLER,                /* 78 — RemoveHandler .ext1 .ext2 */
    DIR_ACTION,                        /* 79 — Action media-type cgi-script */
} directive_type_t;

/**
 * Access control order — determines default policy and evaluation order.
 */
typedef enum {
    ORDER_ALLOW_DENY,   /* Default deny, evaluate Allow then Deny */
    ORDER_DENY_ALLOW,   /* Default allow, evaluate Deny then Allow */
} acl_order_t;

/**
 * Brute force protection action type.
 */
typedef enum {
    BF_ACTION_BLOCK,    /* Return 403 Forbidden */
    BF_ACTION_THROTTLE, /* Delay response */
    BF_ACTION_LOG,      /* Log only, do not block or delay */
} bf_action_t;

/**
 * Directive structure — linked-list node.
 *
 * Each node represents a single parsed .htaccess directive. The `name` and
 * `value` fields carry the generic key/value pair used by most directives.
 * The `data` union holds type-specific fields that vary by directive type.
 */
typedef struct htaccess_directive {
    directive_type_t type;
    int line_number;              /* Source file line number (for logging) */

    /* Generic key-value pair (used by most directives) */
    char *name;                   /* Directive/header/variable/MIME type name */
    char *value;                  /* Directive/header/variable value */
    char *env_condition;          /* Header env=VAR condition (NULL if none) */

    /* Type-specific fields */
    union {
        struct {
            acl_order_t order;
        } acl;

        struct {
            int   status_code;    /* HTTP status code (301, 302, etc.) */
            char *pattern;        /* RedirectMatch regex pattern */
        } redirect;

        struct {
            int error_code;       /* HTTP error code (403, 404, 500, etc.) */
        } error_doc;

        struct {
            char *pattern;        /* FilesMatch regex pattern */
            struct htaccess_directive *children; /* Nested directive list */
        } files_match;

        struct {
            int  active;          /* 1=On, 0=Off */
            long duration_sec;    /* Expiration duration in seconds */
        } expires;

        struct {
            char *attribute;      /* SetEnvIf attribute name */
            char *pattern;        /* SetEnvIf regex pattern */
        } envif;

        struct {
            int        enabled;          /* BruteForceProtection On/Off */
            int        allowed_attempts; /* Max allowed attempts */
            int        window_sec;       /* Time window in seconds */
            bf_action_t action;          /* block or throttle */
            int        throttle_ms;      /* Throttle delay in milliseconds */
        } brute_force;

        /* === v2 new fields === */

        /** IfModule container — name stores module name (may include "!" prefix) */
        struct {
            int negated;                          /* 1 = negated condition */
            struct htaccess_directive *children;   /* Nested directive list */
        } ifmodule;

        /** Files exact-match container — name stores filename */
        struct {
            struct htaccess_directive *children;   /* Nested directive list */
        } files;

        /** Options flag bitmap (tri-state: +1=enable, -1=disable, 0=unchanged) */
        struct {
            int indexes;
            int follow_symlinks;
            int multiviews;
            int exec_cgi;
        } options;

        /** Header edit — stores regex pattern for Header edit/edit* operations */
        struct {
            char *edit_pattern;   /* regex pattern for Header edit */
        } header_ext;

        /** RequireAny / RequireAll container */
        struct {
            struct htaccess_directive *children;   /* Nested directive list */
        } require_container;

        /** Limit / LimitExcept container */
        struct {
            char *methods;                         /* Space-separated HTTP methods */
            struct htaccess_directive *children;    /* Nested directive list */
        } limit;

        /** RewriteCond — parsed for print/round-trip, execution by OLS engine */
        struct {
            char *cond_pattern;  /* Condition pattern (regex or -f/-d/-s/-l) */
            char *flags_raw;     /* Raw flag string "[NC,OR]" (NULL if none) */
            int   nocase;        /* [NC] flag */
            int   or_next;       /* [OR] flag (default AND) */
        } rewrite_cond;

        /** RewriteRule — parsed for print/round-trip, execution by OLS engine */
        struct {
            char *pattern;                         /* Regex pattern */
            char *flags_raw;                       /* Raw flag string "[L,QSA,NC]" */
            struct htaccess_directive *conditions;  /* Linked RewriteCond chain */
        } rewrite_rule;

        /** RewriteMap — passed through to OLS engine */
        struct {
            char *map_name;                        /* Map name identifier */
            char *map_type;                        /* "txt" / "rnd" / "int" */
            char *map_source;                      /* File path or builtin name */
        } rewrite_map;

        /** If/ElseIf/Else — conditional block container */
        struct {
            void *condition;                       /* htaccess_expr_t* (opaque) */
            struct htaccess_directive *children;    /* Nested directive list */
        } if_block;
    } data;

    struct htaccess_directive *next; /* Next node in linked list */
} htaccess_directive_t;

/**
 * Free an entire directive linked list.
 *
 * Walks the list and frees all dynamically allocated memory including
 * name, value, type-specific strings, and nested children (for FilesMatch).
 *
 * @param head  Head of the directive linked list (may be NULL).
 */
void htaccess_directives_free(htaccess_directive_t *head);

/**
 * AllowOverride category bitmask constants.
 * Controls which .htaccess directive types are permitted per-directory.
 * Matches Apache AllowOverride categories.
 */
#define ALLOW_OVERRIDE_NONE      0x00
#define ALLOW_OVERRIDE_LIMIT     0x01  /* Order, Allow, Deny */
#define ALLOW_OVERRIDE_AUTH      0x02  /* AuthType/Name/UserFile, Require*, Satisfy */
#define ALLOW_OVERRIDE_FILEINFO  0x04  /* AddType, ForceType, Redirect*, Rewrite*, ErrorDocument */
#define ALLOW_OVERRIDE_INDEXES   0x08  /* Options Indexes flag */
#define ALLOW_OVERRIDE_OPTIONS   0x10  /* Options (non-Indexes), Limit/LimitExcept, Expires */
#define ALLOW_OVERRIDE_ALL       0xFF  /* All categories permitted */

/**
 * Get the AllowOverride category bitmask for a directive type.
 * Returns ALLOW_OVERRIDE_ALL for unrestricted directives (containers, module extensions).
 */
int directive_category(directive_type_t type);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_DIRECTIVE_H */
