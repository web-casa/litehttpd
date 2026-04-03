/**
 * lsiapi_shim.c - LSIAPI Shim Layer for OLS .htaccess Module
 *
 * This file provides thin wrapper functions that forward calls from the
 * module's direct function-call API (e.g. lsi_session_get_uri()) to the
 * real OLS LSIAPI via the g_api function-pointer table.
 *
 * In OLS, all API functions are accessed through the global `g_api`
 * pointer (of type lsi_api_t*).  Our module source code calls named
 * functions like lsi_session_get_uri(); this shim bridges the two.
 *
 * This file is linked ONLY into the production .so build.
 * Tests link against mock_lsiapi.cpp instead.
 */

#include "ls.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* g_api is provided by OLS at runtime via dlopen */
extern __thread const lsi_api_t *g_api;

/* Defensive NULL guard — all shim functions must check g_api before use.
 * In production OLS always sets g_api before hooks fire, but guard anyway. */
#define GUARD_API_PTR()  do { if (!g_api) return NULL; } while(0)
#define GUARD_API_INT()  do { if (!g_api) return -1; } while(0)
#define GUARD_API_VOID() do { if (!g_api) return; } while(0)

/* ------------------------------------------------------------------ */
/*  Request URI                                                        */
/* ------------------------------------------------------------------ */

const char *lsi_session_get_uri(lsi_session_t *session, int *uri_len)
{
    GUARD_API_PTR();
    return g_api->get_req_uri((const lsi_session_t *)session, uri_len);
}

/* ------------------------------------------------------------------ */
/*  Document root                                                      */
/* ------------------------------------------------------------------ */

const char *lsi_session_get_doc_root(lsi_session_t *session, int *len)
{
    GUARD_API_PTR();
    /* OLS provides doc root via get_req_var_by_id with LSI_VAR_DOC_ROOT */
    static __thread char doc_root_buf[4096];
    int ret = g_api->get_req_var_by_id((const lsi_session_t *)session,
                                        LSI_VAR_DOC_ROOT,
                                        doc_root_buf, sizeof(doc_root_buf));
    if (ret > 0) {
        if (len) *len = ret;
        return doc_root_buf;
    }
    if (len) *len = 0;
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Client IP                                                          */
/* ------------------------------------------------------------------ */

const char *lsi_session_get_client_ip(lsi_session_t *session, int *len)
{
    GUARD_API_PTR();
    return g_api->get_client_ip((const lsi_session_t *)session, len);
}

/* ------------------------------------------------------------------ */
/*  Request headers                                                    */
/* ------------------------------------------------------------------ */

const char *lsi_session_get_req_header_by_name(lsi_session_t *session,
                                                const char *name, int name_len,
                                                int *val_len)
{
    GUARD_API_PTR();
    return g_api->get_req_header((const lsi_session_t *)session,
                                  name, name_len, val_len);
}

int lsi_session_set_req_header(lsi_session_t *session,
                               const char *name, int name_len,
                               const char *val, int val_len)
{
    /* OLS lsi_api_t does not expose a direct request header write function.
     * CyberPanel's approach: set the request header value as an environment
     * variable with the HTTP_* prefix, so the backend (lsphp) sees it via
     * $_SERVER['HTTP_*']. This is the same mechanism Apache uses to pass
     * request headers to CGI/FCGI backends.
     *
     * Example: RequestHeader set X-Forwarded-Proto https
     *   -> env var: HTTP_X_FORWARDED_PROTO = https
     */
    if (!g_api || !name || name_len <= 0)
        return -1;

    /* Build HTTP_<NAME> env key (uppercase, hyphens → underscores) */
    char env_name[512];
    int prefix_len = 5; /* "HTTP_" */
    if (name_len + prefix_len >= (int)sizeof(env_name))
        return -1;

    memcpy(env_name, "HTTP_", 5);
    for (int i = 0; i < name_len; i++) {
        char c = name[i];
        if (c == '-') c = '_';
        else if (c >= 'a' && c <= 'z') c = c - ('a' - 'A');
        env_name[prefix_len + i] = c;
    }
    env_name[prefix_len + name_len] = '\0';

    g_api->set_req_env((const lsi_session_t *)session,
                        env_name, prefix_len + name_len,
                        val, val_len);
    return 0;
}

int lsi_session_remove_req_header(lsi_session_t *session,
                                  const char *name, int name_len)
{
    /* Remove by setting empty value */
    return lsi_session_set_req_header(session, name, name_len, "", 0);
}

/* ------------------------------------------------------------------ */
/*  Response headers                                                   */
/* ------------------------------------------------------------------ */

const char *lsi_session_get_resp_header_by_name(lsi_session_t *session,
                                                 const char *name, int name_len,
                                                 int *val_len)
{
    GUARD_API_PTR();
    static __thread struct iovec iov[1];
    static __thread char hdr_buf[4096];
    int count = g_api->get_resp_header((const lsi_session_t *)session,
                                        LSI_RSPHDR_UNKNOWN,
                                        name, name_len, iov, 1);
    if (count > 0 && iov[0].iov_len > 0) {
        /* iov data is NOT null-terminated — copy to buffer */
        int len = (int)iov[0].iov_len;
        if (len >= (int)sizeof(hdr_buf))
            len = (int)sizeof(hdr_buf) - 1;
        memcpy(hdr_buf, iov[0].iov_base, len);
        hdr_buf[len] = '\0';
        if (val_len) *val_len = len;
        return hdr_buf;
    }
    if (val_len) *val_len = 0;
    return NULL;
}

int lsi_session_set_resp_header(lsi_session_t *session,
                                const char *name, int name_len,
                                const char *val, int val_len)
{
    GUARD_API_INT();
    return g_api->set_resp_header((const lsi_session_t *)session,
                                   LSI_RSPHDR_UNKNOWN,
                                   name, name_len, val, val_len,
                                   LSI_HEADEROP_SET);
}

int lsi_session_set_resp_content_type(lsi_session_t *session,
                                       const char *val, int val_len)
{
    GUARD_API_INT();
    /* Remove existing Content-Type first (OLS native MIME mapping may
     * have set one via the engine, which LSI_HEADEROP_SET alone doesn't
     * replace — it adds a second header instead).
     * Ignore remove failure (header may not exist yet). */
    if (g_api->remove_resp_header)
        g_api->remove_resp_header((const lsi_session_t *)session,
                                    LSI_RSPHDR_CONTENT_TYPE,
                                    NULL, 0);
    int rc = g_api->set_resp_header((const lsi_session_t *)session,
                                     LSI_RSPHDR_CONTENT_TYPE,
                                     NULL, 0, val, val_len,
                                     LSI_HEADEROP_SET);
    /* Fallback: if indexed set failed, try name-based set */
    if (rc != 0 && g_api->set_resp_header)
        rc = g_api->set_resp_header((const lsi_session_t *)session,
                                     LSI_RSPHDR_UNKNOWN,
                                     "Content-Type", 12, val, val_len,
                                     LSI_HEADEROP_SET);
    return rc;
}

const char *lsi_session_get_resp_content_type(lsi_session_t *session,
                                               int *val_len)
{
    GUARD_API_PTR();
    static __thread struct iovec ct_iov[1];
    static __thread char ct_buf[2048];
    int count = g_api->get_resp_header((const lsi_session_t *)session,
                                        LSI_RSPHDR_CONTENT_TYPE,
                                        NULL, 0, ct_iov, 1);
    if (count > 0 && ct_iov[0].iov_len > 0) {
        int len = (int)ct_iov[0].iov_len;
        if (len >= (int)sizeof(ct_buf))
            len = (int)sizeof(ct_buf) - 1;
        memcpy(ct_buf, ct_iov[0].iov_base, len);
        ct_buf[len] = '\0';
        if (val_len) *val_len = len;
        return ct_buf;
    }
    /* Fall back to name-based lookup (pre-engine headers) */
    return lsi_session_get_resp_header_by_name(session,
                                                "Content-Type", 12, val_len);
}

int lsi_session_add_resp_header(lsi_session_t *session,
                                const char *name, int name_len,
                                const char *val, int val_len)
{
    GUARD_API_INT();
    return g_api->set_resp_header((const lsi_session_t *)session,
                                   LSI_RSPHDR_UNKNOWN,
                                   name, name_len, val, val_len,
                                   LSI_HEADEROP_ADD);
}

int lsi_session_append_resp_header(lsi_session_t *session,
                                   const char *name, int name_len,
                                   const char *val, int val_len)
{
    GUARD_API_INT();
    return g_api->set_resp_header((const lsi_session_t *)session,
                                   LSI_RSPHDR_UNKNOWN,
                                   name, name_len, val, val_len,
                                   LSI_HEADEROP_APPEND);
}

int lsi_session_remove_resp_header(lsi_session_t *session,
                                   const char *name, int name_len)
{
    GUARD_API_INT();
    return g_api->remove_resp_header((const lsi_session_t *)session,
                                      LSI_RSPHDR_UNKNOWN,
                                      name, name_len);
}

int lsi_session_get_resp_header_count(lsi_session_t *session,
                                       const char *name, int name_len)
{
    GUARD_API_INT();
    struct iovec iov[16];
    return g_api->get_resp_header((const lsi_session_t *)session,
                                   LSI_RSPHDR_UNKNOWN,
                                   name, name_len, iov, 16);
}

/* ------------------------------------------------------------------ */
/*  Environment variables                                              */
/* ------------------------------------------------------------------ */

const char *lsi_session_get_env(lsi_session_t *session,
                                const char *name, int name_len,
                                int *val_len)
{
    GUARD_API_PTR();
    static __thread char env_buf[4096];
    int ret = g_api->get_req_env((const lsi_session_t *)session,
                                  name, (unsigned int)name_len,
                                  env_buf, sizeof(env_buf));
    if (ret > 0) {
        if (val_len) *val_len = ret;
        return env_buf;
    }
    if (val_len) *val_len = 0;
    return NULL;
}

int lsi_session_set_env(lsi_session_t *session,
                        const char *name, int name_len,
                        const char *val, int val_len)
{
    GUARD_API_INT();
    g_api->set_req_env((const lsi_session_t *)session,
                        name, (unsigned int)name_len, val, val_len);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Redirect (via set_uri_qs — works in URI_MAP phase)                 */
/* ------------------------------------------------------------------ */

int lsi_session_redirect(lsi_session_t *session,
                         int status_code,
                         const char *url, int url_len)
{
    if (!g_api || !url || url_len <= 0)
        return -1;

    /* Set status code and Location header, then end response.
     * set_uri_qs with redirect codes doesn't reliably bypass OLS's
     * file mapping stage when called from URI_MAP hook. Instead,
     * directly set the response to a redirect. */
    g_api->set_status_code((const lsi_session_t *)session, status_code);
    g_api->set_resp_header((const lsi_session_t *)session,
                            LSI_RSPHDR_LOCATION,
                            "Location", 8, url, url_len,
                            LSI_HEADEROP_SET);
    g_api->end_resp((const lsi_session_t *)session);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Response status                                                    */
/* ------------------------------------------------------------------ */

int lsi_session_get_status(lsi_session_t *session)
{
    GUARD_API_INT();
    return g_api->get_status_code((const lsi_session_t *)session);
}

int lsi_session_set_status(lsi_session_t *session, int code)
{
    GUARD_API_INT();
    g_api->set_status_code((const lsi_session_t *)session, code);
    return 0;
}

void lsi_session_end_resp(lsi_session_t *session)
{
    GUARD_API_VOID();
    g_api->end_resp((const lsi_session_t *)session);
}

/* ------------------------------------------------------------------ */
/*  Response body                                                      */
/* ------------------------------------------------------------------ */

int lsi_session_set_resp_body(lsi_session_t *session,
                              const char *buf, int len)
{
    GUARD_API_INT();
    return g_api->append_resp_body((const lsi_session_t *)session, buf, len);
}

/* ------------------------------------------------------------------ */
/*  PHP configuration                                                  */
/* ------------------------------------------------------------------ */

int lsi_session_set_php_ini(lsi_session_t *session,
                            const char *name, const char *val,
                            int type)
{
    if (!g_api || !name || !name[0])
        return -1;

    /* Dual-mode PHPConfig (matches CyberPanel's custom OLS API):
     *
     * 1) If OLS has been patched (set_php_config_value != NULL), use
     *    native LSIAPI which calls HttpSession::setPhpConfigValue()
     *    -> PHPConfig::parse() -> PHPConfig::buildLsapiEnv()
     *
     * 2) Otherwise, fall back to PHP_VALUE / PHP_ADMIN_VALUE env vars
     *    which LSPHP picks up via the LSAPI protocol.
     *
     * type: 0=php_value, 1=php_flag, 2=php_admin_value, 3=php_admin_flag */

    int is_admin = (type >= PHP_INI_TYPE_ADMIN_VALUE);

    /* --- Native API path (patched OLS / CyberPanel custom OLS) --- */
    if (g_api->set_php_config_value) {
        if (type == PHP_INI_TYPE_FLAG || type == PHP_INI_TYPE_ADMIN_FLAG) {
            /* Flags: convert to int for native API */
            int flag_val = 0;
            if (val) {
                if (strcmp(val, "on") == 0 || strcmp(val, "On") == 0 ||
                    strcmp(val, "ON") == 0 ||
                    strcmp(val, "1") == 0 || strcmp(val, "true") == 0 ||
                    strcmp(val, "True") == 0 || strcmp(val, "yes") == 0 ||
                    strcmp(val, "Yes") == 0)
                    flag_val = 1;
            }
            /* PHPConfig type mapping (from phpconfig.h):
             * PHP_VALUE=1, PHP_FLAG=2, PHP_ADMIN_VALUE=3, PHP_ADMIN_FLAG=4 */
            int api_type = is_admin ? 4 : 2;
            g_api->set_php_config_flag(
                (const lsi_session_t *)session, name, flag_val, api_type);
        } else {
            /* Values: pass string directly */
            int api_type = is_admin ? 3 : 1;
            g_api->set_php_config_value(
                (const lsi_session_t *)session, name,
                val ? val : "", api_type);
        }
        /* Fall through to ALSO set env var — belt and suspenders.
         * Native API may not propagate to lsphp in all OLS versions,
         * env-var is the reliable path via LSAPI protocol. */
    }

    /* --- Env-var fallback (stock OLS) --- */
    /* LSPHP reads PHP_VALUE as newline-separated "name=value" entries.
     * Multiple php_value directives must be accumulated, not overwritten. */
    static const char env_admin[] = "PHP_ADMIN_VALUE";
    static const char env_user[]  = "PHP_VALUE";
    const char *env_key = is_admin ? env_admin : env_user;
    int env_key_len     = is_admin ? (int)(sizeof(env_admin) - 1)
                                   : (int)(sizeof(env_user) - 1);

    /* Read existing value to append to it.
     * LSPHP reads PHP_VALUE as newline-separated "name=value" entries. */
    char existing_buf[4096] = {0};
    int existing_len = g_api->get_req_env(
        (const lsi_session_t *)session, env_key, (unsigned int)env_key_len,
        existing_buf, (int)sizeof(existing_buf));
    if (existing_len < 0)
        existing_len = 0;

    /* Build "existing\nname=value" or just "name=value" if first */
    char php_val[4096];
    int pv_len;
    if (existing_len > 0)
        pv_len = snprintf(php_val, sizeof(php_val), "%.*s\n%s=%s",
                          existing_len, existing_buf,
                          name, val ? val : "");
    else
        pv_len = snprintf(php_val, sizeof(php_val), "%s=%s",
                          name, val ? val : "");
    if (pv_len < 0 || pv_len >= (int)sizeof(php_val))
        return -1;

    g_api->set_req_env((const lsi_session_t *)session,
                        env_key, env_key_len,
                        php_val, pv_len);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Hook registration                                                  */
/* ------------------------------------------------------------------ */

/* Hook registration in OLS is done via the serverhook array in lsi_module_t,
 * not via a runtime function call. Our init_cb currently calls
 * lsi_register_hook() which doesn't exist in OLS.
 *
 * We need to switch to using the serverhook array instead.
 * For now, provide a stub that stores the hooks for later use. */

/* We'll store up to 8 hooks */
#define MAX_SHIM_HOOKS 8
static struct {
    int hook_point;
    lsi_callback_pf cb;
    short priority;
} s_shim_hooks[MAX_SHIM_HOOKS];
static int s_shim_hook_count = 0;

int lsi_register_hook(int hook_point, lsi_hook_cb cb, int priority)
{
    /* In the shim, we can't dynamically register hooks.
     * Hooks must be registered via the serverhook array.
     * This function is a no-op in production — we'll use serverhook instead. */
    if (s_shim_hook_count < MAX_SHIM_HOOKS) {
        s_shim_hooks[s_shim_hook_count].hook_point = hook_point;
        s_shim_hooks[s_shim_hook_count].cb = (lsi_callback_pf)(void*)cb;
        s_shim_hooks[s_shim_hook_count].priority = (short)priority;
        s_shim_hook_count++;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Logging                                                            */
/* ------------------------------------------------------------------ */

void lsi_log(lsi_session_t *session, int level, const char *fmt, ...)
{
    if (!g_api || !g_api->vlog)
        return;

    /* Map our simple log levels to OLS log levels */
    int ols_level;
    switch (level) {
    case 0: /* LSI_LOG_DEBUG */ ols_level = 7000; break;
    case 1: /* LSI_LOG_INFO  */ ols_level = 6000; break;
    case 2: /* LSI_LOG_WARN  */ ols_level = 4000; break;
    case 3: /* LSI_LOG_ERROR */ ols_level = 3000; break;
    default: ols_level = 6000; break;
    }

    va_list args;
    va_start(args, fmt);
    g_api->vlog((const lsi_session_t *)session, ols_level, fmt, args, 0);
    va_end(args);
}

/* ------------------------------------------------------------------ */
/*  v2 extensions                                                      */
/* ------------------------------------------------------------------ */

int lsi_session_set_dir_option(lsi_session_t *session,
                               const char *option, int enabled)
{
    GUARD_API_INT();
    /* Try native context option setter first (requires patched OLS).
     * set_req_header is repurposed as context option setter in patch. */
    if (g_api->set_req_header) {
        const char *val = enabled ? "1" : "0";
        if (strcasecmp(option, "Indexes") == 0) {
            if (!enabled) {
                g_api->set_req_header((const lsi_session_t *)session,
                    "autoindex_off", 13, "1", 1, 0);
            } else {
                g_api->set_req_header((const lsi_session_t *)session,
                    "autoindex_on", 12, "1", 1, 0);
            }
            return 0;
        }
    }
    /* Fallback: set env var hint for non-patched OLS */
    char env_val[2] = { enabled ? '1' : '0', '\0' };
    char env_name[256];
    int n = snprintf(env_name, sizeof(env_name), "DIR_OPT_%s", option);
    if (n > 0 && n < (int)sizeof(env_name))
        g_api->set_req_env((const lsi_session_t *)session,
                            env_name, n, env_val, 1);
    return 0;
}

int lsi_session_get_dir_option(lsi_session_t *session,
                               const char *option)
{
    (void)session; (void)option;
    return -1; /* Not available in production */
}

int lsi_session_set_uri_internal(lsi_session_t *session,
                                 const char *uri, int uri_len)
{
    GUARD_API_INT();
    return g_api->set_uri_qs((const lsi_session_t *)session,
                              2 /* LSI_URL_REDIRECT_INTERNAL */,
                              uri, uri_len, NULL, 0);
}

int lsi_session_file_exists(lsi_session_t *session, const char *path)
{
    GUARD_API_INT();
    struct stat st;
    int ret = g_api->get_file_stat((const lsi_session_t *)session,
                                    path, (int)strlen(path), &st);
    return (ret == 0) ? 1 : 0;
}

const char *lsi_session_get_method(lsi_session_t *session, int *len)
{
    GUARD_API_PTR();
    static __thread char method_buf[32];
    int ret = g_api->get_req_var_by_id((const lsi_session_t *)session,
                                        5 /* LSI_VAR_REQ_METHOD */,
                                        method_buf, sizeof(method_buf));
    if (ret > 0) {
        if (len) *len = ret;
        return method_buf;
    }
    if (len) *len = 0;
    return NULL;
}

const char *lsi_session_get_auth_header(lsi_session_t *session, int *len)
{
    GUARD_API_PTR();
    return g_api->get_req_header_by_id((const lsi_session_t *)session,
                                        4 /* LSI_HDR_AUTHORIZATION */,
                                        len);
}

const char *lsi_session_get_query_string(lsi_session_t *session, int *len)
{
    if (!session || !len)
        return NULL;
    GUARD_API_PTR();
    int n = 0;
    const char *qs = g_api->get_req_query_string(
        (const lsi_session_t *)session, &n);
    *len = n;
    return qs;
}

int lsi_session_set_www_authenticate(lsi_session_t *session,
                                     const char *realm, int realm_len)
{
    GUARD_API_INT();
    /* Reject realm containing quotes, CR/LF, or backslash to prevent
     * header injection and malformed WWW-Authenticate values */
    for (int i = 0; i < realm_len; i++) {
        if (realm[i] == '"' || realm[i] == '\\' ||
            realm[i] == '\r' || realm[i] == '\n')
            return -1;
    }
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "Basic realm=\"%.*s\"",
                     realm_len, realm);
    if (n <= 0 || n >= (int)sizeof(buf))
        return -1;
    return g_api->set_resp_header((const lsi_session_t *)session,
                                   LSI_RSPHDR_WWW_AUTHENTICATE,
                                   "WWW-Authenticate", 16,
                                   buf, n, LSI_HEADEROP_SET);
}
