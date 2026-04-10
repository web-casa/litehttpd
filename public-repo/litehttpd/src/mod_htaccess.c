/**
 * mod_htaccess.c - OLS .htaccess Module Entry Point and Hook Callbacks
 *
 * Implements the LSIAPI module descriptor, initialization/cleanup,
 * request-phase hook (access control, redirect, PHP config, env vars,
 * brute force protection), and response-phase hook (headers, expires,
 * error documents, FilesMatch).
 *
 * Validates: Requirements 1.1, 1.2, 1.3, 1.4, 2.1, 2.4, 4.1-4.7,
 *            6.6, 7.5, 8.1-8.4, 9.1-9.3, 10.1-10.3, 14.1-14.4
 */

#include "ls.h"
#include "htaccess_cache.h"
#include "htaccess_shm.h"
#include "htaccess_dirwalker.h"
#include "htaccess_directive.h"
#include "htaccess_exec_acl.h"
#include "htaccess_exec_redirect.h"
#include "htaccess_exec_php.h"
#include "htaccess_exec_env.h"
#include "htaccess_exec_brute_force.h"
#include "htaccess_exec_header.h"
#include "htaccess_exec_expires.h"
#include "htaccess_exec_error_doc.h"
#include "htaccess_exec_files_match.h"
#include "htaccess_exec_options.h"
#include "htaccess_exec_require.h"
#include "htaccess_exec_limit.h"
#include "htaccess_exec_rewrite.h"
#include "htaccess_exec_auth.h"
#include "htaccess_exec_handler.h"
#include "htaccess_exec_dirindex.h"
#include "htaccess_exec_forcetype.h"
#include "htaccess_exec_encoding.h"
#include "htaccess_expr.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define MOD_HTACCESS_CACHE_BUCKETS  64
#define MOD_HTACCESS_SHM_MAX_RECORDS 1024
#define MOD_HTACCESS_HOOK_PRIORITY  100

/* ------------------------------------------------------------------ */
/*  Per-request directive cache (avoids double dirwalk)                 */
/* ------------------------------------------------------------------ */

/**
 * Thread-local cache of the last dirwalk result.
 * OLS workers are single-threaded event loops, so URI_MAP and
 * SEND_RESP_HEADER for the same request run sequentially in the
 * same thread. We cache the merged directives from URI_MAP and
 * reuse them in SEND_RESP_HEADER if the session pointer matches.
 */
static __thread struct {
    const lsi_session_t  *session;     /* session identity */
    htaccess_directive_t *directives;  /* cached merged result (owned) */
} req_cache = { NULL, NULL };

static void req_cache_store(const lsi_session_t *session,
                            htaccess_directive_t *dirs)
{
    /* Always free previous entry (session pointers may be recycled by OLS) */
    if (req_cache.directives && req_cache.directives != dirs)
        htaccess_directives_free(req_cache.directives);
    req_cache.session = session;
    req_cache.directives = dirs;
}

static htaccess_directive_t *req_cache_get(const lsi_session_t *session)
{
    if (req_cache.session == session && req_cache.directives)
        return req_cache.directives;
    return NULL;
}

static void req_cache_clear(void)
{
    if (req_cache.directives)
        htaccess_directives_free(req_cache.directives);
    req_cache.session = NULL;
    req_cache.directives = NULL;
}

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

static int mod_htaccess_init(lsi_module_t *module);
int mod_htaccess_cleanup(lsi_module_t *module);
static int on_uri_map(lsi_session_t *session);
static int on_http_begin(lsi_session_t *session);
static int on_send_resp_header(lsi_session_t *session);

/* OLS hook callbacks receive lsi_param_t*, extract session from it */
static int on_uri_map_hook(lsi_param_t *param)
{
    return on_uri_map((lsi_session_t *)param->session);
}

static int on_http_begin_hook(lsi_param_t *param)
{
    return on_http_begin((lsi_session_t *)param->session);
}

static int on_send_resp_header_hook(lsi_param_t *param)
{
    return on_send_resp_header((lsi_session_t *)param->session);
}

/* ------------------------------------------------------------------ */
/*  Server hook array — declares hooks at load time (OLS style)        */
/* ------------------------------------------------------------------ */

static int on_main_atexit_hook(lsi_param_t *param)
{
    (void)param;
    return mod_htaccess_cleanup(NULL);
}

static lsi_serverhook_t server_hooks[] = {
    /* URI_MAP: ACL, redirects, PHP config, env vars, RequestHeader, brute force */
    { LSI_HKPT_URI_MAP, on_uri_map_hook,
      MOD_HTACCESS_HOOK_PRIORITY, LSI_FLAG_ENABLED },
    /* HTTP_BEGIN: ErrorDocument local-path (internal redirect on error status) */
    { LSI_HKPT_HTTP_BEGIN, on_http_begin_hook,
      MOD_HTACCESS_HOOK_PRIORITY, LSI_FLAG_ENABLED },
    /* SEND_RESP_HEADER: response headers, expires, AddType/ForceType */
    { LSI_HKPT_SEND_RESP_HEADER, on_send_resp_header_hook,
      MOD_HTACCESS_HOOK_PRIORITY, LSI_FLAG_ENABLED },
    /* MAIN_ATEXIT: cleanup cache and SHM */
    { LSI_HKPT_MAIN_ATEXIT, on_main_atexit_hook,
      MOD_HTACCESS_HOOK_PRIORITY, LSI_FLAG_ENABLED },
    LSI_HOOK_END
};

/* ------------------------------------------------------------------ */
/*  Module descriptor (19.1)                                           */
/* ------------------------------------------------------------------ */

LSMODULE_EXPORT lsi_module_t MNAME = {
    LSI_MODULE_SIGNATURE,
    mod_htaccess_init,      /* init_pf callback */
    NULL,                   /* reqhandler (not used) */
    NULL,                   /* config_parser */
    "ols-htaccess v1.0",    /* about */
    server_hooks,           /* serverhook array */
    {0}                     /* reserved */
};

/* ------------------------------------------------------------------ */
/*  Helper: extract target directory from URI                          */
/*  Strips the filename component, keeping only the directory path.    */
/*  Returns a malloc'd string the caller must free.                    */
/* ------------------------------------------------------------------ */

static char *build_target_dir(const char *doc_root, int doc_root_len,
                              const char *uri, int uri_len)
{
    /* Find last '/' in URI to strip filename */
    int dir_len = uri_len;
    while (dir_len > 0 && uri[dir_len - 1] != '/')
        dir_len--;
    /* dir_len now includes the trailing '/' */
    if (dir_len == 0)
        dir_len = 1; /* at least "/" */

    /* Strip trailing slash from doc_root if present */
    int dr_len = doc_root_len;
    while (dr_len > 0 && doc_root[dr_len - 1] == '/')
        dr_len--;

    /* Strip leading slash from URI dir portion to avoid double slash */
    const char *uri_part = uri;
    int uri_part_len = dir_len;
    if (uri_part_len > 0 && uri_part[0] == '/') {
        uri_part++;
        uri_part_len--;
    }

    /* Allocate: doc_root + "/" + uri_dir + NUL */
    size_t total = (size_t)dr_len + 1 + (size_t)uri_part_len + 1;
    char *result = (char *)malloc(total);
    if (!result)
        return NULL;

    memcpy(result, doc_root, (size_t)dr_len);
    result[dr_len] = '/';
    if (uri_part_len > 0)
        memcpy(result + dr_len + 1, uri_part, (size_t)uri_part_len);
    result[dr_len + 1 + uri_part_len] = '\0';

    /* Path traversal protection: resolve and verify within doc_root */
    char resolved_target[PATH_MAX];

    /* Cache resolved doc_root — it rarely changes between requests */
    static __thread char cached_doc_root[PATH_MAX];
    static __thread char cached_resolved_root[PATH_MAX];
    static __thread int  cached_root_valid = 0;
    static __thread size_t cached_root_rlen = 0;

    /* Invalidate cache if doc_root changed */
    if (!cached_root_valid ||
        strcmp(cached_doc_root, doc_root) != 0) {
        if (!realpath(doc_root, cached_resolved_root)) {
            /* doc_root can't be resolved — don't cache, retry next request.
             * Use raw doc_root for this request's ".." traversal check. */
            cached_root_valid = 0;
            strncpy(cached_resolved_root, doc_root, PATH_MAX - 1);
            cached_resolved_root[PATH_MAX - 1] = '\0';
            cached_root_rlen = strlen(cached_resolved_root);
        } else {
            strncpy(cached_doc_root, doc_root, PATH_MAX - 1);
            cached_doc_root[PATH_MAX - 1] = '\0';
            cached_root_rlen = strlen(cached_resolved_root);
            cached_root_valid = 1;
        }
    }

    if (!realpath(result, resolved_target)) {
        /* Target doesn't fully resolve — find the longest existing parent,
         * resolve it, and validate it stays within doc_root. This catches
         * symlink escapes in existing parent components. */
        char parent[PATH_MAX];
        strncpy(parent, result, sizeof(parent) - 1);
        parent[sizeof(parent) - 1] = '\0';

        /* Walk up to find an existing ancestor */
        char *slash = parent + strlen(parent);
        while (slash > parent) {
            *slash = '\0';
            slash--;
            while (slash > parent && *slash != '/') slash--;
            if (slash > parent) {
                char resolved_parent[PATH_MAX];
                if (realpath(parent, resolved_parent)) {
                    /* Validate parent is within doc_root (with boundary check) */
                    if (strncmp(resolved_parent, cached_resolved_root,
                                cached_root_rlen) != 0 ||
                        (resolved_parent[cached_root_rlen] != '/' &&
                         resolved_parent[cached_root_rlen] != '\0')) {
                        free(result);
                        return NULL;
                    }
                    return result; /* Parent verified OK */
                }
            }
        }
        /* No existing parent resolved — fall back to raw ".." scan.
         * This handles virtual/NFS/test paths where nothing is resolvable. */
        {
            const char *p = result;
            while (*p) {
                if (*p == '/') { p++; continue; }
                const char *seg_end = strchr(p, '/');
                size_t seg_len = seg_end ? (size_t)(seg_end - p) : strlen(p);
                if (seg_len == 2 && p[0] == '.' && p[1] == '.') {
                    free(result);
                    return NULL;
                }
                p += seg_len;
            }
        }
        return result;
    }

    if (strncmp(resolved_target, cached_resolved_root, cached_root_rlen) != 0) {
        /* Path traversal detected — target is outside doc_root */
        free(result);
        return NULL;
    }

    /* Ensure it's not just a prefix match (e.g. /var/www vs /var/www2) */
    if (resolved_target[cached_root_rlen] != '/' && resolved_target[cached_root_rlen] != '\0') {
        free(result);
        return NULL;
    }

    return result;
}

/* ------------------------------------------------------------------ */
/*  Helper: extract filename from URI                                  */
/*  Returns pointer into the URI string (no allocation).               */
/* ------------------------------------------------------------------ */

static const char *extract_filename(const char *uri, int uri_len)
{
    int i = uri_len;
    while (i > 0 && uri[i - 1] != '/')
        i--;
    return uri + i;
}

/* ------------------------------------------------------------------ */
/*  Logging helpers (19.4)                                             */
/* ------------------------------------------------------------------ */

/**
 * Log a successful directive application at DEBUG level.
 * Format: "Applying directive <type> at <filepath>:<line>"
 */
static void log_directive_ok(lsi_session_t *session,
                             const htaccess_directive_t *dir,
                             const char *type_str)
{
    const char *file = dir->name ? dir->name : "(unknown)";
    lsi_log(session, LSI_LOG_DEBUG,
            "Applying directive %s at %s:%d",
            type_str, file, dir->line_number);
}

/**
 * Log a directive failure at WARN level.
 * Format: "Directive <type> failed at <filepath>:<line>: <reason>"
 */
static void log_directive_fail(lsi_session_t *session,
                               const htaccess_directive_t *dir,
                               const char *type_str,
                               const char *reason)
{
    const char *file = dir->name ? dir->name : "(unknown)";
    lsi_log(session, LSI_LOG_WARN,
            "Directive %s failed at %s:%d: %s",
            type_str, file, dir->line_number, reason);
}

/**
 * Return a human-readable string for a directive type.
 */
static const char *directive_type_str(directive_type_t type)
{
    switch (type) {
    case DIR_HEADER_SET:                    return "Header set";
    case DIR_HEADER_UNSET:                  return "Header unset";
    case DIR_HEADER_APPEND:                 return "Header append";
    case DIR_HEADER_MERGE:                  return "Header merge";
    case DIR_HEADER_ADD:                    return "Header add";
    case DIR_REQUEST_HEADER_SET:            return "RequestHeader set";
    case DIR_REQUEST_HEADER_UNSET:          return "RequestHeader unset";
    case DIR_PHP_VALUE:                     return "php_value";
    case DIR_PHP_FLAG:                      return "php_flag";
    case DIR_PHP_ADMIN_VALUE:               return "php_admin_value";
    case DIR_PHP_ADMIN_FLAG:                return "php_admin_flag";
    case DIR_ORDER:                         return "Order";
    case DIR_ALLOW_FROM:                    return "Allow from";
    case DIR_DENY_FROM:                     return "Deny from";
    case DIR_REDIRECT:                      return "Redirect";
    case DIR_REDIRECT_MATCH:                return "RedirectMatch";
    case DIR_ERROR_DOCUMENT:                return "ErrorDocument";
    case DIR_FILES_MATCH:                   return "FilesMatch";
    case DIR_EXPIRES_ACTIVE:                return "ExpiresActive";
    case DIR_EXPIRES_BY_TYPE:               return "ExpiresByType";
    case DIR_SETENV:                        return "SetEnv";
    case DIR_SETENVIF:                      return "SetEnvIf";
    case DIR_BROWSER_MATCH:                 return "BrowserMatch";
    case DIR_BRUTE_FORCE_PROTECTION:        return "BruteForceProtection";
    case DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS:  return "BruteForceAllowedAttempts";
    case DIR_BRUTE_FORCE_WINDOW:            return "BruteForceWindow";
    case DIR_BRUTE_FORCE_ACTION:            return "BruteForceAction";
    case DIR_BRUTE_FORCE_THROTTLE_DURATION: return "BruteForceThrottleDuration";
    /* v2 types */
    case DIR_IFMODULE:                      return "IfModule";
    case DIR_OPTIONS:                       return "Options";
    case DIR_FILES:                         return "Files";
    case DIR_HEADER_ALWAYS_SET:             return "Header always set";
    case DIR_HEADER_ALWAYS_UNSET:           return "Header always unset";
    case DIR_HEADER_ALWAYS_APPEND:          return "Header always append";
    case DIR_HEADER_ALWAYS_MERGE:           return "Header always merge";
    case DIR_HEADER_ALWAYS_ADD:             return "Header always add";
    case DIR_EXPIRES_DEFAULT:               return "ExpiresDefault";
    case DIR_REQUIRE_ALL_GRANTED:           return "Require all granted";
    case DIR_REQUIRE_ALL_DENIED:            return "Require all denied";
    case DIR_REQUIRE_IP:                    return "Require ip";
    case DIR_REQUIRE_NOT_IP:                return "Require not ip";
    case DIR_REQUIRE_ANY_OPEN:              return "RequireAny";
    case DIR_REQUIRE_ALL_OPEN:              return "RequireAll";
    case DIR_LIMIT:                         return "Limit";
    case DIR_LIMIT_EXCEPT:                  return "LimitExcept";
    case DIR_AUTH_TYPE:                      return "AuthType";
    case DIR_AUTH_NAME:                      return "AuthName";
    case DIR_AUTH_USER_FILE:                 return "AuthUserFile";
    case DIR_REQUIRE_VALID_USER:            return "Require valid-user";
    case DIR_REQUIRE_ENV:                  return "Require env";
    case DIR_ADD_HANDLER:                   return "AddHandler";
    case DIR_SET_HANDLER:                   return "SetHandler";
    case DIR_REMOVE_TYPE:                  return "RemoveType";
    case DIR_REMOVE_HANDLER:               return "RemoveHandler";
    case DIR_ACTION:                       return "Action";
    case DIR_ADD_TYPE:                      return "AddType";
    case DIR_DIRECTORY_INDEX:               return "DirectoryIndex";
    case DIR_FORCE_TYPE:                    return "ForceType";
    case DIR_ADD_ENCODING:                  return "AddEncoding";
    case DIR_ADD_CHARSET:                   return "AddCharset";
    case DIR_BRUTE_FORCE_X_FORWARDED_FOR:   return "BruteForceXForwardedFor";
    case DIR_BRUTE_FORCE_WHITELIST:         return "BruteForceWhitelist";
    case DIR_BRUTE_FORCE_PROTECT_PATH:      return "BruteForceProtectPath";
    case DIR_SETENVIF_NOCASE:               return "SetEnvIfNoCase";
    case DIR_HEADER_EDIT:                   return "Header edit";
    case DIR_HEADER_EDIT_STAR:              return "Header edit*";
    case DIR_HEADER_ALWAYS_EDIT:            return "Header always edit";
    case DIR_HEADER_ALWAYS_EDIT_STAR:       return "Header always edit*";
    case DIR_REWRITE_ENGINE:                return "RewriteEngine";
    case DIR_REWRITE_BASE:                  return "RewriteBase";
    case DIR_REWRITE_COND:                  return "RewriteCond";
    case DIR_REWRITE_RULE:                  return "RewriteRule";
    case DIR_ADD_DEFAULT_CHARSET:           return "AddDefaultCharset";
    case DIR_DEFAULT_TYPE:                  return "DefaultType";
    case DIR_SATISFY:                       return "Satisfy";
    case DIR_REWRITE_OPTIONS:               return "RewriteOptions";
    case DIR_REWRITE_MAP:                   return "RewriteMap";
    case DIR_IF:                            return "If";
    case DIR_ELSEIF:                        return "ElseIf";
    case DIR_ELSE:                          return "Else";
    default:                                return "Unknown";
    }
}

/* ------------------------------------------------------------------ */
/*  Module init / cleanup (19.1)                                       */
/* ------------------------------------------------------------------ */

/**
 * Module initialization — called by LSIAPI when the module is loaded.
 * Initializes cache and shared memory, registers hook callbacks.
 */
static int mod_htaccess_init(lsi_module_t *module)
{
    (void)module;

    /* Initialize cache */
    if (htaccess_cache_init(MOD_HTACCESS_CACHE_BUCKETS) != 0) {
        lsi_log(NULL, LSI_LOG_ERROR,
                "mod_htaccess: failed to initialize cache");
        return LSI_ERROR;
    }

    /* Initialize shared memory for brute force protection */
    if (shm_init("/dev/shm/ols/", MOD_HTACCESS_SHM_MAX_RECORDS) != 0) {
        lsi_log(NULL, LSI_LOG_WARN,
                "mod_htaccess: failed to initialize shared memory, "
                "brute force protection will be disabled");
        /* Non-fatal: continue without brute force protection */
    }

    /* Hooks are registered via the serverhook array in the module descriptor,
     * not via lsi_register_hook() calls.  OLS reads the array at load time. */

    /* Detect RewriteRule support (requires custom OLS with rewrite patch) */
    if (g_api && g_api->parse_rewrite_rules &&
        g_api->exec_rewrite_rules && g_api->free_rewrite_rules) {
        lsi_log(NULL, LSI_LOG_INFO,
                "mod_htaccess: RewriteRule support enabled (custom OLS detected)");
    } else {
        lsi_log(NULL, LSI_LOG_INFO,
                "mod_htaccess: RewriteRule parsing only — execution requires "
                "custom OLS with rewrite patch (patches/0002-lsiapi-rewrite.patch)");
    }

    /* Detect PHPConfig support */
    if (g_api && g_api->set_php_config_value) {
        lsi_log(NULL, LSI_LOG_INFO,
                "mod_htaccess: PHPConfig native API enabled (custom OLS detected)");
    } else {
        lsi_log(NULL, LSI_LOG_INFO,
                "mod_htaccess: php_value/php_flag via env-var fallback "
                "(native API requires custom OLS with PHPConfig patch)");
    }

    lsi_log(NULL, LSI_LOG_INFO,
            "mod_htaccess: module initialized successfully (71 directive types)");
    return LSI_OK;
}

/**
 * Module cleanup — releases cache and shared memory.
 */
int mod_htaccess_cleanup(lsi_module_t *module)
{
    (void)module;
    req_cache_clear();
    rewrite_cache_cleanup();
    htaccess_cache_destroy();
    shm_destroy();
    lsi_log(NULL, LSI_LOG_INFO, "mod_htaccess: module cleaned up");
    return LSI_OK;
}

/* ------------------------------------------------------------------ */
/*  Container ACL helper                                               */
/* ------------------------------------------------------------------ */

/**
 * Check ACL children of a container directive (<Files>, <FilesMatch>, <Limit>).
 * Combines legacy Order/Allow/Deny with modern Require ip/not ip/RequireAny/RequireAll.
 * Returns 1 if access should be denied, 0 otherwise.
 */
static int container_acl_denies(const htaccess_directive_t *children,
                                lsi_session_t *session,
                                const char *client_ip)
{
    int has_require_all_granted = 0;
    int has_require_directives = 0;
    const htaccess_directive_t *child;

    for (child = children; child; child = child->next) {
        switch (child->type) {
        case DIR_REQUIRE_ALL_DENIED:
            return 1;
        case DIR_REQUIRE_ALL_GRANTED:
            has_require_all_granted = 1;
            break;
        case DIR_REQUIRE_IP:
        case DIR_REQUIRE_NOT_IP:
        case DIR_REQUIRE_VALID_USER:
        case DIR_REQUIRE_ENV:
        case DIR_REQUIRE_ANY_OPEN:
        case DIR_REQUIRE_ALL_OPEN:
            has_require_directives = 1;
            break;
        case DIR_IF: {
            /* Evaluate If/ElseIf/Else chain; recursively check matched branch */
            int branch_taken = 0;
            const htaccess_directive_t *d = child;
            while (d && (d->type == DIR_IF || d->type == DIR_ELSEIF || d->type == DIR_ELSE)) {
                if (!branch_taken) {
                    int should_eval = 0;
                    if (d->type == DIR_ELSE) {
                        should_eval = 1;
                    } else {
                        htaccess_expr_t *expr = (htaccess_expr_t *)d->data.if_block.condition;
                        should_eval = (eval_expr(session, expr) > 0);
                    }
                    if (should_eval) {
                        branch_taken = 1;
                        int rc = container_acl_denies(d->data.if_block.children,
                                                       session, client_ip);
                        if (rc) return 1;
                    }
                }
                d = d->next;
            }
            break;
        }
        case DIR_LIMIT:
        case DIR_LIMIT_EXCEPT: {
            /* Recursively check nested Limit/LimitExcept */
            int mlen = 0;
            const char *meth = lsi_session_get_method(session, &mlen);
            if (meth && limit_should_exec(child, meth)) {
                int rc = container_acl_denies(child->data.limit.children,
                                               session, client_ip);
                if (rc) return 1;
            }
            break;
        }
        default:
            break;
        }
    }

    /* Modern Require directives take precedence over legacy ACL */
    if (has_require_all_granted)
        return 0;

    /* If there are Require ip/not ip/valid-user/RequireAny/RequireAll, delegate to exec_require */
    if (has_require_directives) {
        int rc = exec_require(session, children, client_ip,
                              check_auth_credentials(session, children));
        if (rc == LSI_ERROR)
            return 1;
        return 0;
    }

    /* Legacy ACL evaluation — use the full Order/Allow/Deny evaluator */
    {
        int has_legacy_acl = 0;
        for (child = children; child; child = child->next) {
            if (child->type == DIR_ORDER || child->type == DIR_ALLOW_FROM ||
                child->type == DIR_DENY_FROM) {
                has_legacy_acl = 1;
                break;
            }
        }
        if (has_legacy_acl) {
            int rc = exec_access_control(session, children);
            if (rc == LSI_ERROR)
                return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  If/ElseIf/Else evaluation helper                                   */
/* ------------------------------------------------------------------ */

/**
 * Evaluate an If/ElseIf/Else chain starting at *cursor.
 * Advances *cursor past the consumed chain (past trailing ElseIf/Else).
 * Calls exec_fn for each child directive in the matching branch.
 *
 * exec_fn receives session + directive; returns void (best-effort).
 */
typedef int (*if_child_exec_fn)(lsi_session_t *session,
                                  const htaccess_directive_t *child);

static int eval_if_chain(lsi_session_t *session,
                           const htaccess_directive_t **cursor,
                           if_child_exec_fn exec_fn)
{
    const htaccess_directive_t *d = *cursor;
    int branch_taken = 0;
    int terminal = 0;

    /* d must be DIR_IF */
    if (d->type == DIR_IF) {
        htaccess_expr_t *expr = (htaccess_expr_t *)d->data.if_block.condition;
        int result = eval_expr(session, expr);
        if (result > 0) {
            branch_taken = 1;
            for (const htaccess_directive_t *child = d->data.if_block.children;
                 child; child = child->next) {
                int rc = exec_fn(session, child);
                if (rc != 0) { terminal = rc; break; }
            }
        }
        d = d->next;
    }

    /* Consume subsequent ElseIf/Else */
    while (d && (d->type == DIR_ELSEIF || d->type == DIR_ELSE)) {
        if (!branch_taken) {
            if (d->type == DIR_ELSEIF) {
                htaccess_expr_t *expr = (htaccess_expr_t *)d->data.if_block.condition;
                int result = eval_expr(session, expr);
                if (result > 0) {
                    branch_taken = 1;
                    for (const htaccess_directive_t *child = d->data.if_block.children;
                         child; child = child->next) {
                        int rc = exec_fn(session, child);
                        if (rc != 0) { terminal = rc; break; }
                    }
                }
            } else { /* DIR_ELSE */
                branch_taken = 1;
                for (const htaccess_directive_t *child = d->data.if_block.children;
                     child; child = child->next) {
                    int rc = exec_fn(session, child);
                    if (rc != 0) { terminal = rc; break; }
                }
            }
        }
        d = d->next;
    }

    *cursor = d;
    return terminal;
}

/**
 * Request-phase child executor for If/ElseIf/Else children.
 * Handles SetEnv, RequestHeader, PHP directives within conditional blocks.
 */
static int exec_if_child_request(lsi_session_t *session,
                                   const htaccess_directive_t *child)
{
    int rc;
    switch (child->type) {
    case DIR_SETENV:
        exec_setenv(session, child);
        return 0;
    case DIR_SETENVIF:
    case DIR_SETENVIF_NOCASE:
        exec_setenvif(session, child);
        return 0;
    case DIR_BROWSER_MATCH:
        exec_browser_match(session, child);
        return 0;
    case DIR_REQUEST_HEADER_SET:
    case DIR_REQUEST_HEADER_UNSET:
        exec_request_header(session, child);
        return 0;
    case DIR_PHP_VALUE:
        exec_php_value(session, child);
        return 0;
    case DIR_PHP_FLAG:
        exec_php_flag(session, child);
        return 0;
    case DIR_PHP_ADMIN_VALUE:
        exec_php_admin_value(session, child);
        return 0;
    case DIR_PHP_ADMIN_FLAG:
        exec_php_admin_flag(session, child);
        return 0;
    case DIR_REDIRECT:
        rc = exec_redirect(session, child);
        return (rc > 0) ? 1 : 0; /* Redirect succeeded → short-circuit */
    case DIR_REDIRECT_MATCH:
        rc = exec_redirect_match(session, child);
        return (rc > 0) ? 1 : 0;
    case DIR_OPTIONS:
        exec_options(session, child);
        return 0;
    /* Auth directives are handled as a group by eval_if_chain after
     * all children are processed — skip individually here */
    case DIR_AUTH_TYPE:
    case DIR_AUTH_NAME:
    case DIR_AUTH_USER_FILE:
    case DIR_REQUIRE_VALID_USER:
    case DIR_REQUIRE_ALL_GRANTED:
    case DIR_REQUIRE_ALL_DENIED:
    case DIR_REQUIRE_IP:
    case DIR_REQUIRE_NOT_IP:
    case DIR_REQUIRE_ENV:
    case DIR_SATISFY:
        return 0; /* Handled by post-branch auth check */
    case DIR_IF:
    case DIR_ELSEIF:
    case DIR_ELSE: {
        const htaccess_directive_t *cursor = child;
        return eval_if_chain(session, &cursor, exec_if_child_request);
    }
    case DIR_REWRITE_ENGINE:
    case DIR_REWRITE_BASE:
    case DIR_REWRITE_COND:
    case DIR_REWRITE_RULE:
    case DIR_REWRITE_OPTIONS:
    case DIR_REWRITE_MAP:
        /* Rewrite directives inside <If> blocks are not supported.
         * Apache evaluates <If expr> at a late fixup phase where rewrite
         * has already run. OLS processes rewrite via a separate engine
         * (processRuleSet) that only sees top-level directives.
         * Log once to help debugging; not an error since most real-world
         * .htaccess files don't put RewriteRule inside <If>. */
        lsi_log(session, LSI_LOG_DEBUG,
                "mod_htaccess: Rewrite inside <If> not supported, ignored");
        return 0;
    default:
        return 0;
    }
}

/**
 * Response-phase child executor for If/ElseIf/Else children.
 * Handles Header, ErrorDocument, and MIME directives within conditional blocks.
 */
static int exec_if_child_response(lsi_session_t *session,
                                    const htaccess_directive_t *child)
{
    switch (child->type) {
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
        exec_header(session, child);
        return 0;
    case DIR_ERROR_DOCUMENT:
        exec_error_document(session, child);
        return 0;
    case DIR_FORCE_TYPE:
        exec_force_type(session, child);
        return 0;
    case DIR_DEFAULT_TYPE:
        if (child->value) {
            int ct_len = 0;
            const char *ct = lsi_session_get_resp_content_type(session, &ct_len);
            if (!ct || ct_len == 0)
                lsi_session_set_resp_content_type(session,
                    child->value, (int)strlen(child->value));
        }
        return 0;
    case DIR_ADD_DEFAULT_CHARSET:
        /* Inline: append charset to text/* Content-Type if not already set */
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
        return 0;
    case DIR_ADD_TYPE: {
        /* Need filename for extension matching — extract from URI */
        int uri_len = 0;
        const char *uri = lsi_session_get_uri(session, &uri_len);
        const char *fn = extract_filename(uri, uri_len);
        if (fn) exec_add_type(session, child, fn);
        return 0;
    }
    case DIR_ADD_ENCODING: {
        int uri_len = 0;
        const char *uri = lsi_session_get_uri(session, &uri_len);
        const char *fn = extract_filename(uri, uri_len);
        if (fn) exec_add_encoding(session, child, fn);
        return 0;
    }
    case DIR_ADD_CHARSET: {
        int uri_len = 0;
        const char *uri = lsi_session_get_uri(session, &uri_len);
        const char *fn = extract_filename(uri, uri_len);
        if (fn) exec_add_charset(session, child, fn);
        return 0;
    }
    case DIR_EXPIRES_ACTIVE:
    case DIR_EXPIRES_BY_TYPE:
    case DIR_EXPIRES_DEFAULT:
        /* Expires inside <If> — handled by exec_expires after If chain.
         * The If chain now runs before Expires, so ForceType/DefaultType
         * changes take effect. Expires directives themselves are collected
         * and executed by the top-level Expires loop, which already
         * iterates all directives including those in parsed If results.
         * No action needed here — just acknowledge. */
        return 0;
    case DIR_IF:
    case DIR_ELSEIF:
    case DIR_ELSE: {
        const htaccess_directive_t *cursor = child;
        return eval_if_chain(session, &cursor, exec_if_child_response);
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */

/**
 * on_uri_map — called at LSI_HKPT_RECV_REQ_HEADER.
 *
 * Flow:
 * 1. Get doc_root and request URI
 * 2. Build target directory (strip filename from URI)
 * 3. Call DirWalker to collect merged directives
 * 4. Execute request-phase directives in order:
 *    a. Access control — return immediately on deny
 *    b. Redirects — return immediately on match
 *    c. PHP configuration
 *    d. Environment variables
 *    e. Brute force protection
 * 5. Free directives and return
 */
static int on_uri_map(lsi_session_t *session)
{
    /* Clear any stale per-request cache from a previous request.
     * OLS may recycle session pointers, so a stale entry keyed by the
     * same pointer value could leak directives from a prior request. */
    req_cache_clear();

    /* Reset per-request state */
    exec_header_reset_shadow();

    int doc_root_len = 0;
    const char *doc_root = lsi_session_get_doc_root(session, &doc_root_len);
    if (!doc_root || doc_root_len <= 0) {
        lsi_log(session, LSI_LOG_DEBUG,
                "mod_htaccess: no document root, skipping");
        return LSI_OK;
    }

    int uri_len = 0;
    const char *uri = lsi_session_get_uri(session, &uri_len);
    if (!uri || uri_len <= 0) {
        lsi_log(session, LSI_LOG_DEBUG,
                "mod_htaccess: no request URI, skipping");
        return LSI_OK;
    }

    /* Block access to .ht* files (defense-in-depth, matches Apache default).
     * Apache has <Files ".ht*"> Require all denied in core config.
     * OLS does not, so we enforce it here. */
    {
        const char *fname = extract_filename(uri, uri_len);
        if (fname && fname[0] == '.' && fname[1] == 'h' && fname[2] == 't') {
            lsi_log(session, LSI_LOG_INFO,
                    "mod_htaccess: blocked access to %s (protected .ht* file)",
                    uri);
            lsi_session_set_status(session, 403);
            lsi_session_end_resp(session);
            return LSI_DENY;
        }
    }

    /* Block path traversal attempts.
     * OLS normalizes ../ before our hook, so check the ORIGINAL URI
     * (before rewrite) for suspicious patterns. Also percent-decode
     * to catch %2e%2e, %2f, and mixed encoding bypasses. */
    {
        char org_uri[4096];
        int org_len = 0;
        if (g_api && g_api->get_req_org_uri) {
            org_len = g_api->get_req_org_uri(session, org_uri, sizeof(org_uri) - 1);
            if (org_len > 0) org_uri[org_len] = '\0';
        }
        const char *check_uri = (org_len > 0) ? org_uri : uri;
        int check_len = (org_len > 0) ? org_len : uri_len;

        /* Percent-decode into a scratch buffer for traversal detection */
        char decoded[4096];
        int di = 0;
        for (int i = 0; i < check_len && di < (int)sizeof(decoded) - 1; i++) {
            if (check_uri[i] == '%' && i + 2 < check_len) {
                int hi = check_uri[i + 1];
                int lo = check_uri[i + 2];
                /* Convert hex digits */
                int h = (hi >= '0' && hi <= '9') ? hi - '0' :
                         (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 :
                         (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 : -1;
                int l = (lo >= '0' && lo <= '9') ? lo - '0' :
                         (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 :
                         (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 : -1;
                if (h >= 0 && l >= 0) {
                    decoded[di++] = (char)((h << 4) | l);
                    i += 2;
                    continue;
                }
            }
            /* Also normalize backslash to forward slash */
            decoded[di++] = (check_uri[i] == '\\') ? '/' : check_uri[i];
        }
        decoded[di] = '\0';

        /* Check decoded URI for path traversal patterns:
         * /../, /..\, leading ../, trailing /.. */
        for (int i = 0; i < di - 1; i++) {
            if (decoded[i] == '.' && decoded[i + 1] == '.') {
                /* Verify it's a path segment boundary */
                int before_ok = (i == 0 || decoded[i - 1] == '/');
                int after_ok = (i + 2 >= di || decoded[i + 2] == '/' || decoded[i + 2] == '?' || decoded[i + 2] == '\0');
                if (before_ok && after_ok) {
                    lsi_log(session, LSI_LOG_INFO,
                            "mod_htaccess: blocked path traversal: %.*s",
                            check_len, check_uri);
                    lsi_session_set_status(session, 403);
                    lsi_session_end_resp(session);
                    return LSI_DENY;
                }
            }
        }
    }

    /* Block PHP execution in WordPress uploads directory.
     * Matches Apache behavior: /wp-content/uploads/ auto-disables PHP.
     * Prevents uploaded malicious PHP files from executing.
     * Scans the full URI for .php within uploads path to prevent
     * PATH_INFO bypass (e.g., /wp-content/uploads/shell.php/anything). */
    {
        const char *uploads = strstr(uri, "/wp-content/uploads/");
        if (uploads) {
            const char *scan = uploads + 20; /* skip "/wp-content/uploads/" */
            while (scan < uri + uri_len) {
                const char *dot = strstr(scan, ".php");
                if (!dot) break;
                /* Check it's a real extension: followed by \0, /, ? or more extension */
                char after = dot[4];
                if (after == '\0' || after == '/' || after == '?' ||
                    after == '5' || after == '7') { /* .php5, .php7 */
                    lsi_log(session, LSI_LOG_INFO,
                            "mod_htaccess: blocked PHP in uploads: %s", uri);
                    lsi_session_set_status(session, 403);
                    lsi_session_end_resp(session);
                    return LSI_DENY;
                }
                scan = dot + 4;
            }
            /* Also check .phtml and .phar */
            if (strstr(uploads, ".phtml") || strstr(uploads, ".phar")) {
                lsi_log(session, LSI_LOG_INFO,
                        "mod_htaccess: blocked PHP in uploads: %s", uri);
                lsi_session_set_status(session, 403);
                lsi_session_end_resp(session);
                return LSI_DENY;
            }
        }
    }

    /* Build target directory path.
     * Returns NULL on both allocation failure and path escape detection.
     * Since path escape is a security boundary, treat NULL as 403 (fail-closed)
     * rather than LSI_OK (fail-open). The path traversal defense above already
     * catches most escapes, so false positives from allocation failure are
     * extremely rare (only when malloc fails). */
    char *target_dir = build_target_dir(doc_root, doc_root_len, uri, uri_len);
    if (!target_dir) {
        lsi_log(session, LSI_LOG_WARN,
                "mod_htaccess: target dir build failed (path escape or alloc), denying");
        lsi_session_set_status(session, 403);
        lsi_session_end_resp(session);
        return LSI_DENY;
    }


    /* Collect merged directives via DirWalker */
    htaccess_directive_t *directives = htaccess_dirwalk(session, doc_root,
                                                         target_dir);

    if (!directives) {
        lsi_log(session, LSI_LOG_DEBUG,
                "mod_htaccess: no directives found for request");
        req_cache_clear();  /* Prevent stale cache from prior request */
        free(target_dir);
        return LSI_OK;
    }

    /* (a) Access control + Authentication with Satisfy support.
     *
     * Satisfy All (default): both ACL and Auth must pass.
     * Satisfy Any: either ACL or Auth passing is sufficient.
     *
     * Apache 2.4 Require directives take precedence over legacy ACL.
     * When Require is present, Satisfy is ignored (Require has its own logic). */
    int ip_len = 0;
    const char *client_ip = lsi_session_get_client_ip(session, &ip_len);

    /* Determine Satisfy mode */
    int satisfy_any = 0; /* default: Satisfy All */
    {
        const htaccess_directive_t *scan;
        for (scan = directives; scan; scan = scan->next) {
            if (scan->type == DIR_SATISFY && scan->value) {
                satisfy_any = (strcasecmp(scan->value, "Any") == 0);
                break;
            }
        }
    }

    /* Scan for any Require directive */
    int has_require = 0;
    {
        const htaccess_directive_t *scan;
        for (scan = directives; scan; scan = scan->next) {
            if (scan->type == DIR_REQUIRE_ALL_GRANTED ||
                scan->type == DIR_REQUIRE_ALL_DENIED ||
                scan->type == DIR_REQUIRE_IP ||
                scan->type == DIR_REQUIRE_NOT_IP ||
                scan->type == DIR_REQUIRE_VALID_USER ||
                scan->type == DIR_REQUIRE_ENV ||
                scan->type == DIR_REQUIRE_ANY_OPEN ||
                scan->type == DIR_REQUIRE_ALL_OPEN) {
                has_require = 1;
                break;
            }
        }
    }

    int acl_failed = 0;

    if (has_require) {
        /* Modern Require takes precedence — skip legacy ACL.
         * Satisfy is ignored when Require is present. */
        int rc = exec_require(session, directives, client_ip,
                              check_auth_credentials(session, directives));
        if (rc == LSI_ERROR) {
            lsi_log(session, LSI_LOG_DEBUG,
                    "mod_htaccess: access denied by Require");
            lsi_session_set_status(session, 403);
            lsi_session_end_resp(session);
            free(target_dir);
            htaccess_directives_free(directives);
            return LSI_DENY;
        }
    } else {
        /* No Require directives — use legacy Order/Allow/Deny */
        int rc = exec_access_control(session, directives);
        if (rc == LSI_ERROR) {
            if (satisfy_any) {
                /* Satisfy Any: ACL failed, but Auth might still pass */
                acl_failed = 1;
                lsi_log(session, LSI_LOG_DEBUG,
                        "mod_htaccess: ACL denied, trying auth (Satisfy Any)");
            } else {
                lsi_log(session, LSI_LOG_DEBUG,
                        "mod_htaccess: access denied by ACL");
                lsi_session_set_status(session, 403);
                lsi_session_end_resp(session);
                free(target_dir);
                htaccess_directives_free(directives);
                return LSI_DENY;
            }
        }
    }

    /* (a3) <Files> and <FilesMatch> block access control */
    {
        const char *req_filename = extract_filename(uri, uri_len);
        const htaccess_directive_t *fdir;
        for (fdir = directives; fdir != NULL; fdir = fdir->next) {
            /* <Files exact_name> — exact filename match */
            if (fdir->type == DIR_FILES && fdir->name && req_filename) {
                if (strcmp(fdir->name, req_filename) == 0) {
                    if (container_acl_denies(fdir->data.files.children,
                                             session, client_ip)) {
                        lsi_log(session, LSI_LOG_DEBUG,
                                "mod_htaccess: access denied by <Files %s>",
                                fdir->name);
                        lsi_session_set_status(session, 403);
                        lsi_session_end_resp(session);
                        free(target_dir);
                        htaccess_directives_free(directives);
                        return LSI_DENY;
                    }
                    /* Also check auth for container children */
                    int auth_rc = exec_auth_basic(session, fdir->data.files.children);
                    if (auth_rc == LSI_ERROR) {
                        if (client_ip && ip_len > 0)
                            exec_brute_force(session, directives, client_ip, 1);
                        free(target_dir);
                        htaccess_directives_free(directives);
                        return LSI_DENY;
                    }
                }
            }
            /* <FilesMatch regex> — regex filename match */
            if (fdir->type == DIR_FILES_MATCH && req_filename &&
                fdir->data.files_match.pattern) {
                int matched = fm_regex_matches(
                    fdir->data.files_match.pattern, req_filename);
                if (matched == 1) {
                    if (container_acl_denies(fdir->data.files_match.children,
                                             session, client_ip)) {
                        lsi_log(session, LSI_LOG_DEBUG,
                                "mod_htaccess: access denied by <FilesMatch %s>",
                                fdir->data.files_match.pattern);
                        lsi_session_set_status(session, 403);
                        lsi_session_end_resp(session);
                        free(target_dir);
                        htaccess_directives_free(directives);
                        return LSI_DENY;
                    }
                    /* Also check auth for container children */
                    int auth_rc = exec_auth_basic(session,
                                                   fdir->data.files_match.children);
                    if (auth_rc == LSI_ERROR) {
                        if (client_ip && ip_len > 0)
                            exec_brute_force(session, directives, client_ip, 1);
                        free(target_dir);
                        htaccess_directives_free(directives);
                        return LSI_DENY;
                    }
                }
            }
        }
    }

    /* (a4) Limit/LimitExcept method restriction */
    int method_len = 0;
    const char *http_method = lsi_session_get_method(session, &method_len);
    const htaccess_directive_t *dir;
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type == DIR_LIMIT || dir->type == DIR_LIMIT_EXCEPT) {
            if (http_method && limit_should_exec(dir, http_method)) {
                /* Execute ACL children — full Require + legacy ACL support */
                if (container_acl_denies(dir->data.limit.children,
                                         session, client_ip)) {
                    lsi_session_set_status(session, 403);
                    lsi_session_end_resp(session);
                    free(target_dir);
                    htaccess_directives_free(directives);
                    return LSI_DENY;
                }
                /* Also check auth for Limit children */
                int auth_rc = exec_auth_basic(session, dir->data.limit.children);
                if (auth_rc == LSI_ERROR) {
                    if (client_ip && ip_len > 0)
                        exec_brute_force(session, directives, client_ip, 1);
                    free(target_dir);
                    htaccess_directives_free(directives);
                    return LSI_DENY;
                }
            }
        }
    }

    /* (a5) AuthType Basic authentication + brute force integration.
     * With Satisfy Any: if ACL passed, skip auth. If ACL failed, auth
     * must pass. With Satisfy All (default): both must pass.
     * When Require is present, Satisfy is ignored — always check auth. */
    if (satisfy_any && !acl_failed && !has_require) {
        /* Satisfy Any + ACL passed + no Require → skip auth check */
        lsi_log(session, LSI_LOG_DEBUG,
                "mod_htaccess: Satisfy Any, ACL passed, skipping auth");
    } else {
        int rc = exec_auth_basic(session, directives);
        if (rc == LSI_ERROR) {
            if (satisfy_any && acl_failed) {
                /* Satisfy Any: both ACL and Auth failed → deny */
                lsi_log(session, LSI_LOG_DEBUG,
                        "mod_htaccess: Satisfy Any, both ACL and auth failed");
            }
            /* Auth failed — record as brute force attempt */
            if (client_ip && ip_len > 0) {
                int bf_rc = exec_brute_force(session, directives, client_ip, 1);
                if (bf_rc == LSI_ERROR) {
                    lsi_log(session, LSI_LOG_DEBUG,
                            "mod_htaccess: auth failed + brute force blocked");
                }
            }
            lsi_log(session, LSI_LOG_DEBUG,
                    "mod_htaccess: authentication failed");
            free(target_dir);
            htaccess_directives_free(directives);
            return LSI_DENY;
        }
        /* Auth passed */
        if (acl_failed) {
            lsi_log(session, LSI_LOG_DEBUG,
                    "mod_htaccess: Satisfy Any, auth passed (ACL had failed)");
        }
    }

    /* (b) Redirects — execute before RewriteRule (Apache order:
     * mod_alias runs in fixup phase before mod_rewrite) */
    for (dir = directives; dir != NULL; dir = dir->next) {
        int redir_rc = 0;
        if (dir->type == DIR_REDIRECT) {
            redir_rc = exec_redirect(session, dir);
            if (redir_rc > 0) {
                log_directive_ok(session, dir, "Redirect");
                free(target_dir);
                req_cache_store(session, directives);
                return LSI_OK;
            } else if (redir_rc < 0) {
                log_directive_fail(session, dir, "Redirect",
                                   "execution error");
            }
        } else if (dir->type == DIR_REDIRECT_MATCH) {
            redir_rc = exec_redirect_match(session, dir);
            if (redir_rc > 0) {
                log_directive_ok(session, dir, "RedirectMatch");
                free(target_dir);
                req_cache_store(session, directives);
                return LSI_OK;
            } else if (redir_rc < 0) {
                log_directive_fail(session, dir, "RedirectMatch",
                                   "execution error");
            }
        }
    }

    /* (c) Rewrite rules — execute after redirects (Apache order) */
    {
        int rw_rc = exec_rewrite_rules(session, directives);
        if (rw_rc > 0) {
            if (rw_rc >= 301 && rw_rc <= 399)
                lsi_log(session, LSI_LOG_DEBUG,
                        "mod_htaccess: rewrite redirect %d", rw_rc);
            else if (rw_rc == 403 || rw_rc == 410 || rw_rc == 500)
                lsi_log(session, LSI_LOG_DEBUG,
                        "mod_htaccess: rewrite returned %d", rw_rc);
            else
                lsi_log(session, LSI_LOG_DEBUG,
                        "mod_htaccess: rewrite matched, URI changed");
            free(target_dir);
            req_cache_store(session, directives);
            return LSI_OK;
        }
    }

    /* (c) PHP configuration */
    for (dir = directives; dir != NULL; dir = dir->next) {
        int php_rc = LSI_OK;
        const char *type_name = directive_type_str(dir->type);
        switch (dir->type) {
        case DIR_PHP_VALUE:
            php_rc = exec_php_value(session, dir);
            break;
        case DIR_PHP_FLAG:
            php_rc = exec_php_flag(session, dir);
            break;
        case DIR_PHP_ADMIN_VALUE:
            php_rc = exec_php_admin_value(session, dir);
            break;
        case DIR_PHP_ADMIN_FLAG:
            php_rc = exec_php_admin_flag(session, dir);
            break;
        default:
            continue;
        }
        if (php_rc == LSI_OK)
            log_directive_ok(session, dir, type_name);
        else
            log_directive_fail(session, dir, type_name, "PHP config error");
    }

    /* (d) Environment variables */
    for (dir = directives; dir != NULL; dir = dir->next) {
        int env_rc = LSI_OK;
        const char *type_name = directive_type_str(dir->type);
        switch (dir->type) {
        case DIR_SETENV:
            env_rc = exec_setenv(session, dir);
            break;
        case DIR_SETENVIF:
        case DIR_SETENVIF_NOCASE:
            env_rc = exec_setenvif(session, dir);
            break;
        case DIR_BROWSER_MATCH:
            env_rc = exec_browser_match(session, dir);
            break;
        default:
            continue;
        }
        if (env_rc == LSI_OK)
            log_directive_ok(session, dir, type_name);
        else
            log_directive_fail(session, dir, type_name, "env var error");
    }

    /* (e) RequestHeader — must run in request phase so backends see the values */
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type == DIR_REQUEST_HEADER_SET ||
            dir->type == DIR_REQUEST_HEADER_UNSET) {
            int rh_rc = exec_request_header(session, dir);
            if (rh_rc == LSI_OK)
                log_directive_ok(session, dir, directive_type_str(dir->type));
            else
                log_directive_fail(session, dir, directive_type_str(dir->type),
                                   "request header error");
        }
    }

    /* (f) Brute force protection — check-only (no counting).
     * Counting only happens on auth failure (see a5 above). */
    if (client_ip && ip_len > 0) {
        int rc = exec_brute_force(session, directives, client_ip, 0);
        if (rc == LSI_ERROR) {
            lsi_log(session, LSI_LOG_DEBUG,
                    "mod_htaccess: request blocked by brute force protection");
            free(target_dir);
            htaccess_directives_free(directives);
            return LSI_DENY;
        }
    }

    /* (g) Options */
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type == DIR_OPTIONS)
            exec_options(session, dir);
    }

    /* (h) DirectoryIndex — only for directory requests (URI ends with /) */
    if (uri_len > 0 && uri[uri_len - 1] == '/') {
        for (dir = directives; dir != NULL; dir = dir->next) {
            if (dir->type == DIR_DIRECTORY_INDEX)
                exec_directory_index(session, dir, target_dir);
        }
    }

    /* (i) If/ElseIf/Else conditional blocks — request-phase children */
    {
        const htaccess_directive_t *scan = directives;
        while (scan) {
            if (scan->type == DIR_IF) {
                /* Run request-phase children + check for terminal action */
                int term = eval_if_chain(session, &scan, exec_if_child_request);
                if (term) {
                    /* Redirect or terminal action occurred — stop processing.
                     * Use LSI_OK (not LSI_DENY) so OLS treats this the same
                     * as top-level redirects: the response is already sent
                     * by exec_redirect/lsi_session_redirect. */
                    free(target_dir);
                    req_cache_store(session, directives);
                    return LSI_OK;
                }
            } else {
                scan = scan->next;
            }
        }
        /* Auth/ACL check for matched If branches (all chains, including Else) */
        scan = directives;
        while (scan) {
            if (scan->type == DIR_IF) {
                int branch_taken = 0;
                const htaccess_directive_t *d = scan;
                while (d && (d->type == DIR_IF || d->type == DIR_ELSEIF || d->type == DIR_ELSE)) {
                    if (!branch_taken) {
                        int should_eval = (d->type == DIR_ELSE) ? 1 :
                            (eval_expr(session, (htaccess_expr_t *)d->data.if_block.condition) > 0);
                        if (should_eval) {
                            branch_taken = 1;
                            /* Full ACL check on matched branch */
                            if (container_acl_denies(d->data.if_block.children,
                                                      session, client_ip)) {
                                lsi_session_set_status(session, 403);
                                lsi_session_end_resp(session);
                                free(target_dir);
                                htaccess_directives_free(directives);
                                return LSI_DENY;
                            }
                            /* Auth check on matched branch */
                            int rc = exec_auth_basic(session, d->data.if_block.children);
                            if (rc == LSI_ERROR) {
                                if (client_ip && ip_len > 0)
                                    exec_brute_force(session, directives, client_ip, 1);
                                free(target_dir);
                                htaccess_directives_free(directives);
                                return LSI_DENY;
                            }
                        }
                    }
                    d = d->next;
                }
                scan = d; /* Skip past consumed chain */
            } else {
                scan = scan->next;
            }
        }
    }

    /* ErrorDocument 404 pre-check: if the requested static file does not exist
     * and the .htaccess has an ErrorDocument 404 directive, handle it here
     * in URI_MAP where status changes and redirects are still effective.
     * This mirrors how exec_redirect works for 301/302 → LSI_DENY.
     *
     * Only applies when the URI is not a directory (no trailing slash) and
     * does not contain a query string (we check the plain file path). */
    {
        /* Build the physical path for the requested URI */
        const char *q = uri;
        int plain_len = uri_len;
        while (plain_len > 0 && q[plain_len - 1] != '?') plain_len--;
        if (plain_len > 0) plain_len--; /* strip '?' */
        else plain_len = uri_len;       /* no query string */

        /* Skip directory requests */
        if (plain_len > 0 && uri[plain_len - 1] != '/') {
            char phys_path[4096];
            snprintf(phys_path, sizeof(phys_path), "%.*s%.*s",
                     doc_root_len, doc_root, plain_len, uri);

            /* Check if file does NOT exist */
            if (!lsi_session_file_exists(session, phys_path)) {
                /* Find ErrorDocument 404 in directives */
                const htaccess_directive_t *d;
                for (d = directives; d; d = d->next) {
                    if (d->type != DIR_ERROR_DOCUMENT)
                        continue;
                    if (d->data.error_doc.error_code != 404 || !d->value)
                        continue;

                    const char *val = d->value;
                    int vlen = (int)strlen(val);

                    /* External URL → 302 redirect (same pattern as exec_redirect).
                     * IMPORTANT: set headers BEFORE freeing directives, because
                     * val points into d->value which is freed by directives_free. */
                    if (strncasecmp(val, "http://", 7) == 0 ||
                        strncasecmp(val, "https://", 8) == 0) {
                        if (!memchr(val, '\r', vlen) &&
                            !memchr(val, '\n', vlen)) {
                            lsi_session_set_status(session, 302);
                            lsi_session_set_resp_header(session,
                                "Location", 8, val, vlen);
                            free(target_dir);
                            htaccess_directives_free(directives);
                            lsi_session_end_resp(session);
                            return LSI_DENY;
                        }
                    }

                    /* Local path → read file, serve as 404 with custom body.
                     * Read file BEFORE freeing directives (val into d->value). */
                    if (val[0] == '/') {
                        /* Reject path traversal */
                        if (strstr(val, "..")) {
                            /* skip this ErrorDocument — path traversal attempt */
                            break;
                        }
                        char err_path[4096];
                        snprintf(err_path, sizeof(err_path), "%.*s%s",
                                 doc_root_len, doc_root, val);
                        FILE *fp = fopen(err_path, "r");
                        if (fp) {
                            char fbuf[65536];
                            size_t n = fread(fbuf, 1, sizeof(fbuf) - 1, fp);
                            fclose(fp);
                            if (n > 0) {
                                lsi_session_set_status(session, 404);
                                lsi_session_set_resp_content_type(
                                    session, "text/html", 9);
                                lsi_session_set_resp_body(session, fbuf, (int)n);
                                free(target_dir);
                                htaccess_directives_free(directives);
                                lsi_session_end_resp(session);
                                return LSI_DENY;
                            }
                        }
                    }

                    /* Quoted inline text → serve as 404 with custom body.
                     * Copy text before freeing directives. */
                    if (val[0] == '"') {
                        const char *text = val + 1;
                        int tlen = vlen - 1;
                        if (tlen > 0 && text[tlen - 1] == '"') tlen--;
                        if (tlen > 0) {
                            lsi_session_set_status(session, 404);
                            lsi_session_set_resp_body(session, text, tlen);
                            free(target_dir);
                            htaccess_directives_free(directives);
                            lsi_session_end_resp(session);
                            return LSI_DENY;
                        }
                    }
                    break; /* Only one ErrorDocument 404 */
                }
            }
        }
    }

    free(target_dir);
    /* Cache directives for reuse in on_send_resp_header (same request) */
    req_cache_store(session, directives);
    return LSI_OK;
}

/* ------------------------------------------------------------------ */
/*  HTTP_BEGIN hook — ErrorDocument local-path (internal redirect)      */
/* ------------------------------------------------------------------ */

/**
 * on_http_begin — called at LSI_HKPT_HTTP_BEGIN.
 *
 * Checks if the current response status matches any ErrorDocument directive
 * with a local URI path. If so, performs an internal redirect to that URI.
 * This runs early enough for OLS to serve the target URI as the error page.
 *
 * Matches CyberPanel's check_error_document_begin approach.
 */
static int on_http_begin(lsi_session_t *session)
{
    int current_status = lsi_session_get_status(session);

    /* Only process error status codes (4xx, 5xx) */
    if (current_status < 400)
        return LSI_OK;

    int doc_root_len = 0;
    const char *doc_root = lsi_session_get_doc_root(session, &doc_root_len);
    if (!doc_root || doc_root_len <= 0)
        return LSI_OK;

    int uri_len = 0;
    const char *uri = lsi_session_get_uri(session, &uri_len);
    if (!uri || uri_len <= 0)
        return LSI_OK;

    /* Try cached directives from URI_MAP phase first */
    htaccess_directive_t *directives = req_cache_get(session);
    int owned = 0;

    if (!directives) {
        char *target_dir = build_target_dir(doc_root, doc_root_len, uri, uri_len);
        if (!target_dir)
            return LSI_OK;
        directives = htaccess_dirwalk(session, doc_root, target_dir);
        free(target_dir);
        owned = 1;
    }

    if (!directives)
        return LSI_OK;

    /* Scan for ErrorDocument with matching status code and local path */
    const htaccess_directive_t *dir;
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type == DIR_ERROR_DOCUMENT) {
            if (dir->data.error_doc.error_code != current_status)
                continue;
            if (!dir->value || dir->value[0] != '/')
                continue;

            /* Local URI path — internal redirect */
            int val_len = (int)strlen(dir->value);
            lsi_session_set_uri_internal(session, dir->value, val_len);
            lsi_log(session, LSI_LOG_DEBUG,
                    "mod_htaccess: ErrorDocument %d -> %s (internal redirect)",
                    current_status, dir->value);
            if (owned)
                htaccess_directives_free(directives);
            return LSI_OK;
        }
        /* Check If/ElseIf/Else chains for ErrorDocument */
        if (dir->type == DIR_IF) {
            const htaccess_directive_t *d = dir;
            int branch_taken = 0;
            while (d && (d->type == DIR_IF || d->type == DIR_ELSEIF || d->type == DIR_ELSE)) {
                if (!branch_taken) {
                    int should_eval = (d->type == DIR_ELSE) ? 1 :
                        (eval_expr(session, (htaccess_expr_t *)d->data.if_block.condition) > 0);
                    if (should_eval) {
                        branch_taken = 1;
                        const htaccess_directive_t *ic;
                        for (ic = d->data.if_block.children; ic; ic = ic->next) {
                            if (ic->type == DIR_ERROR_DOCUMENT &&
                                ic->data.error_doc.error_code == current_status &&
                                ic->value && ic->value[0] == '/') {
                                int vl = (int)strlen(ic->value);
                                lsi_session_set_uri_internal(session, ic->value, vl);
                                if (owned) htaccess_directives_free(directives);
                                return LSI_OK;
                            }
                        }
                    }
                }
                d = d->next;
            }
        }
    }

    /* Also check Files/FilesMatch children for local-path ErrorDocument */
    {
        const char *req_filename = extract_filename(uri, uri_len);
        for (dir = directives; dir; dir = dir->next) {
            if (dir->type == DIR_FILES && dir->name && req_filename &&
                strcmp(dir->name, req_filename) == 0) {
                const htaccess_directive_t *fc;
                for (fc = dir->data.files.children; fc; fc = fc->next) {
                    if (fc->type == DIR_ERROR_DOCUMENT &&
                        fc->data.error_doc.error_code == current_status &&
                        fc->value && fc->value[0] == '/') {
                        int vl = (int)strlen(fc->value);
                        lsi_session_set_uri_internal(session, fc->value, vl);
                        if (owned) htaccess_directives_free(directives);
                        return LSI_OK;
                    }
                }
            }
            if (dir->type == DIR_FILES_MATCH && req_filename &&
                dir->data.files_match.pattern) {
                if (fm_regex_matches(dir->data.files_match.pattern, req_filename) == 1) {
                    const htaccess_directive_t *fc;
                    for (fc = dir->data.files_match.children; fc; fc = fc->next) {
                        if (fc->type == DIR_ERROR_DOCUMENT &&
                            fc->data.error_doc.error_code == current_status &&
                            fc->value && fc->value[0] == '/') {
                            int vl = (int)strlen(fc->value);
                            lsi_session_set_uri_internal(session, fc->value, vl);
                            if (owned) htaccess_directives_free(directives);
                            return LSI_OK;
                        }
                    }
                }
            }
        }
    }

    if (owned)
        htaccess_directives_free(directives);
    return LSI_OK;
}

/* ------------------------------------------------------------------ */
/*  Response-phase hook callback (19.3)                                */
/* ------------------------------------------------------------------ */

/**
 * on_send_resp_header — called at LSI_HKPT_SEND_RESP_HEADER.
 *
 * Flow:
 * 1. Get doc_root and URI, build target directory
 * 2. Call DirWalker to get merged directives
 * 3. Extract filename from URI for FilesMatch
 * 4. Execute response-phase directives:
 *    a. Header / RequestHeader directives
 *    b. FilesMatch conditional blocks
 *    c. Expires directives
 *    d. ErrorDocument directives
 * 5. Free directives and return
 */
static int on_send_resp_header(lsi_session_t *session)
{
    int doc_root_len = 0;
    const char *doc_root = lsi_session_get_doc_root(session, &doc_root_len);
    if (!doc_root || doc_root_len <= 0)
        return LSI_OK;

    int uri_len = 0;
    const char *uri = lsi_session_get_uri(session, &uri_len);
    if (!uri || uri_len <= 0)
        return LSI_OK;

    /* Try cached directives from URI_MAP phase first */
    htaccess_directive_t *directives = req_cache_get(session);
    int cached = (directives != NULL);

    if (!directives) {
        /* Cache miss — build target dir and do full dirwalk */
        char *target_dir = build_target_dir(doc_root, doc_root_len, uri, uri_len);
        if (!target_dir)
            return LSI_OK;
        directives = htaccess_dirwalk(session, doc_root, target_dir);
        free(target_dir);
    }

    if (!directives)
        return LSI_OK;

    /* Extract filename for FilesMatch */
    const char *filename = extract_filename(uri, uri_len);

    /* (a) Response Header directives (RequestHeader now in request-phase hook) */
    const htaccess_directive_t *dir;
    for (dir = directives; dir != NULL; dir = dir->next) {
        int hdr_rc = LSI_OK;
        const char *type_name = directive_type_str(dir->type);
        switch (dir->type) {
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
            hdr_rc = exec_header(session, dir);
            break;
        default:
            continue;
        }
        if (hdr_rc == LSI_OK)
            log_directive_ok(session, dir, type_name);
        else
            log_directive_fail(session, dir, type_name, "header error");
    }

    /* (a2) Files blocks — exact filename match */
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type == DIR_FILES && dir->name && filename) {
            if (strcmp(dir->name, filename) == 0) {
                /* Execute children directives */
                const htaccess_directive_t *child;
                for (child = dir->data.files.children; child; child = child->next) {
                    switch (child->type) {
                    case DIR_HEADER_SET: case DIR_HEADER_UNSET:
                    case DIR_HEADER_APPEND: case DIR_HEADER_MERGE:
                    case DIR_HEADER_ADD:
                    case DIR_HEADER_ALWAYS_SET: case DIR_HEADER_ALWAYS_UNSET:
                    case DIR_HEADER_ALWAYS_APPEND: case DIR_HEADER_ALWAYS_MERGE:
                    case DIR_HEADER_ALWAYS_ADD:
                    case DIR_HEADER_EDIT: case DIR_HEADER_EDIT_STAR:
                    case DIR_HEADER_ALWAYS_EDIT: case DIR_HEADER_ALWAYS_EDIT_STAR:
                        exec_header(session, child);
                        break;
                    case DIR_ERROR_DOCUMENT:
                        exec_error_document(session, child);
                        break;
                    case DIR_ADD_TYPE:
                        exec_add_type(session, child, filename);
                        break;
                    case DIR_FORCE_TYPE:
                        exec_force_type(session, child);
                        break;
                    case DIR_ADD_ENCODING:
                        exec_add_encoding(session, child, filename);
                        break;
                    case DIR_ADD_CHARSET:
                        exec_add_charset(session, child, filename);
                        break;
                    case DIR_ADD_DEFAULT_CHARSET:
                        if (child->value && strcasecmp(child->value, "Off") != 0) {
                            int ctl = 0;
                            const char *ctype = lsi_session_get_resp_content_type(session, &ctl);
                            if (ctype && ctl > 5 && strncasecmp(ctype, "text/", 5) == 0 &&
                                !strcasestr(ctype, "charset=")) {
                                char buf[256];
                                int bl = snprintf(buf, sizeof(buf), "%.*s; charset=%s",
                                                  ctl, ctype, child->value);
                                if (bl > 0 && bl < (int)sizeof(buf))
                                    lsi_session_set_resp_content_type(session,
                                                                buf, bl);
                            }
                        }
                        break;
                    case DIR_DEFAULT_TYPE:
                        if (child->value) {
                            int ct_len = 0;
                            const char *ct = lsi_session_get_resp_content_type(session, &ct_len);
                            if (!ct || ct_len == 0)
                                lsi_session_set_resp_content_type(session,
                                    child->value, (int)strlen(child->value));
                        }
                        break;
                    case DIR_IF:
                    case DIR_ELSEIF:
                    case DIR_ELSE: {
                        const htaccess_directive_t *cursor = child;
                        eval_if_chain(session, &cursor, exec_if_child_response);
                        break;
                    }
                    default:
                        break;
                    }
                }
            }
        }
    }

    /* (b) FilesMatch conditional blocks */
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type != DIR_FILES_MATCH)
            continue;
        int fm_rc = exec_files_match(session, dir, filename);
        if (fm_rc > 0) {
            log_directive_ok(session, dir, "FilesMatch");
            /* Nested If chains already executed inside exec_files_match
             * in original directive order via dispatch_child */
        } else if (fm_rc < 0) {
            log_directive_fail(session, dir, "FilesMatch",
                               "pattern match error");
        }
        /* fm_rc == 0: no match, skip */
    }

    /* (c) AddType / ForceType / AddEncoding / AddCharset
     * Must run BEFORE Expires so ExpiresByType matches the correct MIME type */

    /* (c0) DefaultType — set Content-Type only if none exists yet */
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type == DIR_DEFAULT_TYPE && dir->value) {
            int ct_len = 0;
            const char *ct = lsi_session_get_resp_content_type(session, &ct_len);
            if (!ct || ct_len == 0) {
                lsi_session_set_resp_content_type(session,
                    dir->value, (int)strlen(dir->value));
                log_directive_ok(session, dir, "DefaultType");
            }
            break; /* singleton */
        }
    }

    /* (c1) RemoveType — clear Content-Type for matched extensions.
     * Runs BEFORE AddType so subdirectory RemoveType cancels parent AddType. */
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type == DIR_REMOVE_TYPE && dir->value && filename) {
            const char *dot = strrchr(filename, '.');
            if (dot) {
                char *exts_copy = strdup(dir->value);
                if (exts_copy) {
                    char *saveptr = NULL;
                    char *ext = strtok_r(exts_copy, " \t", &saveptr);
                    while (ext) {
                        /* Normalize: compare with or without leading dot */
                        const char *cmp = ext;
                        if (cmp[0] == '.') cmp++;
                        if (dot[1] && strcasecmp(dot + 1, cmp) == 0) {
                            lsi_session_remove_resp_header(session,
                                "Content-Type", 12);
                            log_directive_ok(session, dir, "RemoveType");
                            break;
                        }
                        ext = strtok_r(NULL, " \t", &saveptr);
                    }
                    free(exts_copy);
                }
            }
        }
    }

    /* (c2) AddType / ForceType / AddEncoding / AddCharset */
    for (dir = directives; dir != NULL; dir = dir->next) {
        switch (dir->type) {
        case DIR_ADD_TYPE:
            exec_add_type(session, dir, filename);
            break;
        case DIR_FORCE_TYPE:
            exec_force_type(session, dir);
            break;
        case DIR_ADD_ENCODING:
            exec_add_encoding(session, dir, filename);
            break;
        case DIR_ADD_CHARSET:
            exec_add_charset(session, dir, filename);
            break;
        default:
            break;
        }
    }

    /* (c2) AddDefaultCharset — append charset to text/* responses */
    /* Case-insensitive search for "charset=" in Content-Type */
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type == DIR_ADD_DEFAULT_CHARSET && dir->value) {
            if (strcasecmp(dir->value, "Off") == 0)
                break; /* explicitly disabled */
            int ct_len = 0;
            const char *ct = lsi_session_get_resp_content_type(session, &ct_len);
            /* Only apply to text/* without existing charset */
            /* Check if charset= already present (case-insensitive) */
            if (ct && ct_len > 5 &&
                strncasecmp(ct, "text/", 5) == 0) {
                int has_charset = 0;
                for (int j = 0; j < ct_len - 7; j++) {
                    if (strncasecmp(ct + j, "charset=", 8) == 0) {
                        has_charset = 1;
                        break;
                    }
                }
                if (!has_charset) {
                    char buf[256];
                    int n = snprintf(buf, sizeof(buf), "%.*s; charset=%s",
                                     ct_len, ct, dir->value);
                    if (n > 0 && n < (int)sizeof(buf)) {
                        lsi_session_set_resp_content_type(session, buf, n);
                        log_directive_ok(session, dir, "AddDefaultCharset");
                    }
                }
            }
            break; /* singleton */
        }
    }

    /* (d) If/ElseIf/Else conditional blocks — response-phase children.
     * Must run BEFORE Expires so ForceType/DefaultType inside <If> blocks
     * take effect before ExpiresByType matches Content-Type. */
    {
        const htaccess_directive_t *scan = directives;
        while (scan) {
            if (scan->type == DIR_IF) {
                eval_if_chain(session, &scan, exec_if_child_response);
            } else {
                scan = scan->next;
            }
        }
    }

    /* (e) Expires directives — Content-Type now reflects AddType/ForceType
     * AND any conditional type changes from <If> blocks above */
    int ct_iov_len = 0;
    const char *content_type = NULL;
    static __thread char ct_buf[256];
    if (g_api && g_api->get_resp_header) {
        /* Try dedicated Content-Type API first for OLS engine types */
        struct iovec ct_iov[1];
        int ct_count = g_api->get_resp_header((const lsi_session_t *)session,
                                               LSI_RSPHDR_CONTENT_TYPE,
                                               NULL, 0, ct_iov, 1);
        if (ct_count > 0 && ct_iov[0].iov_len > 0) {
            /* iov data is NOT null-terminated — copy to buffer */
            ct_iov_len = (int)ct_iov[0].iov_len;
            if (ct_iov_len >= (int)sizeof(ct_buf))
                ct_iov_len = (int)sizeof(ct_buf) - 1;
            memcpy(ct_buf, ct_iov[0].iov_base, ct_iov_len);
            ct_buf[ct_iov_len] = '\0';
            content_type = ct_buf;
        }
    }
    if (!content_type)
        content_type = lsi_session_get_resp_content_type(session, &ct_iov_len);
    if (!content_type)
        content_type = "application/octet-stream";
    exec_expires(session, directives, content_type);

    /* (f) ErrorDocument directives.
     * NOTE: HTTP_BEGIN always fires with status=200 (before URI_MAP sets the
     * final error status). The on_http_begin error-code check never triggers
     * for normal errors. ErrorDocument processing must happen here in
     * SEND_RESP_HEADER where the final error status is available.
     * Limitation: local URI paths cannot replace OLS's built-in error body
     * because OLS fixes Content-Length before this hook fires. */
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type != DIR_ERROR_DOCUMENT)
            continue;
        int ed_rc = exec_error_document(session, dir);
        if (ed_rc == 0)
            log_directive_ok(session, dir, "ErrorDocument");
        else if (ed_rc < 0)
            log_directive_fail(session, dir, "ErrorDocument",
                               "error document processing failed");
    }

    /* (g) AddHandler / SetHandler / RemoveHandler / Action (no-op stubs) */
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type == DIR_ADD_HANDLER)
            exec_add_handler(session, dir);
        else if (dir->type == DIR_SET_HANDLER)
            exec_set_handler(session, dir);
        else if (dir->type == DIR_REMOVE_HANDLER)
            lsi_log(session, LSI_LOG_DEBUG,
                    "mod_htaccess: RemoveHandler parsed but OLS uses scriptHandler config (no-op)");
        else if (dir->type == DIR_ACTION)
            lsi_log(session, LSI_LOG_DEBUG,
                    "mod_htaccess: Action directive not supported by OLS (no-op)");
    }

    /* Free directives — if from req_cache, clear entry; else free directly */
    if (cached)
        req_cache_clear();
    else
        htaccess_directives_free(directives);
    return LSI_OK;
}
