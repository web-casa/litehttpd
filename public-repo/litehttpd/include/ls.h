/**
 * ls.h - LSIAPI Stub Header for OLS .htaccess Module
 *
 * This header provides the LSIAPI types and constants needed by the module.
 * When compiled for production (.so), functions are resolved via the shim
 * layer (lsiapi_shim.c) which forwards to g_api.
 * When compiled for tests, mock_lsiapi.h defines LS_H first, so this
 * header is skipped entirely.
 */
#ifndef LS_H
#define LS_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/uio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Module signature                                                   */
/* ------------------------------------------------------------------ */

#define LSIAPI_VERSION_MAJOR    1
#define LSIAPI_VERSION_MINOR    2

#define LSI_MODULE_SIGNATURE    ((int64_t)0x4C53494D00000000LL + \
    (int64_t)(LSIAPI_VERSION_MAJOR << 16) + \
    (int64_t)(LSIAPI_VERSION_MINOR))

#define LSMODULE_EXPORT  __attribute__ ((visibility ("default")))

/* ------------------------------------------------------------------ */
/*  Return codes                                                       */
/* ------------------------------------------------------------------ */

#define LSI_OK    0
#define LSI_ERROR (-1)
#define LSI_DENY  (-2)

/* ------------------------------------------------------------------ */
/*  Log levels (module-internal simple values)                         */
/* ------------------------------------------------------------------ */

#define LSI_LOG_DEBUG 0
#define LSI_LOG_INFO  1
#define LSI_LOG_WARN  2
#define LSI_LOG_ERROR 3

/* ------------------------------------------------------------------ */
/*  Hook-point constants (must match OLS enum LSI_HKPT_LEVEL)          */
/* ------------------------------------------------------------------ */

enum {
    LSI_HKPT_L4_BEGINSESSION = 0,
    LSI_HKPT_L4_ENDSESSION,
    LSI_HKPT_L4_RECVING,
    LSI_HKPT_L4_SENDING,
    LSI_HKPT_HTTP_BEGIN,
    LSI_HKPT_RCVD_REQ_HEADER,
    LSI_HKPT_URI_MAP,
    LSI_HKPT_HTTP_AUTH,
    LSI_HKPT_RECV_REQ_BODY,
    LSI_HKPT_RCVD_REQ_BODY,
    LSI_HKPT_RCVD_RESP_HEADER,
    LSI_HKPT_RECV_RESP_BODY,
    LSI_HKPT_RCVD_RESP_BODY,
    LSI_HKPT_HANDLER_RESTART,
    LSI_HKPT_SEND_RESP_HEADER,
    LSI_HKPT_SEND_RESP_BODY,
    LSI_HKPT_HTTP_END,
    LSI_HKPT_MAIN_INITED,
    LSI_HKPT_MAIN_PREFORK,
    LSI_HKPT_MAIN_POSTFORK,
    LSI_HKPT_WORKER_INIT,
    LSI_HKPT_WORKER_ATEXIT,
    LSI_HKPT_MAIN_ATEXIT,
    LSI_HKPT_TOTAL_COUNT
};

/* Aliases used by our module code */
#define LSI_HKPT_RECV_REQ_HEADER  LSI_HKPT_RCVD_REQ_HEADER

/* ------------------------------------------------------------------ */
/*  Data levels                                                        */
/* ------------------------------------------------------------------ */

enum {
    LSI_DATA_HTTP = 0,
    LSI_DATA_FILE,
    LSI_DATA_IP,
    LSI_DATA_VHOST,
    LSI_DATA_L4,
    LSI_DATA_COUNT
};

/* ------------------------------------------------------------------ */
/*  Header operation constants                                         */
/* ------------------------------------------------------------------ */

enum {
    LSI_HEADEROP_SET = 0,
    LSI_HEADEROP_APPEND,
    LSI_HEADEROP_MERGE,
    LSI_HEADEROP_ADD
};

/* ------------------------------------------------------------------ */
/*  Response header IDs                                                */
/* ------------------------------------------------------------------ */

enum {
    LSI_RSPHDR_ACCEPT_RANGES = 0,
    LSI_RSPHDR_CONNECTION,
    LSI_RSPHDR_CONTENT_TYPE,
    LSI_RSPHDR_CONTENT_LENGTH,
    LSI_RSPHDR_CONTENT_ENCODING,
    LSI_RSPHDR_CONTENT_RANGE,
    LSI_RSPHDR_CONTENT_DISPOSITION,
    LSI_RSPHDR_CACHE_CTRL,
    LSI_RSPHDR_DATE,
    LSI_RSPHDR_ETAG,
    LSI_RSPHDR_EXPIRES,
    LSI_RSPHDR_KEEP_ALIVE,
    LSI_RSPHDR_LAST_MODIFIED,
    LSI_RSPHDR_LOCATION,
    LSI_RSPHDR_LITESPEED_LOCATION,
    LSI_RSPHDR_LITESPEED_CACHE_CONTROL,
    LSI_RSPHDR_PRAGMA,
    LSI_RSPHDR_PROXY_CONNECTION,
    LSI_RSPHDR_SERVER,
    LSI_RSPHDR_SET_COOKIE,
    LSI_RSPHDR_CGI_STATUS,
    LSI_RSPHDR_TRANSFER_ENCODING,
    LSI_RSPHDR_VARY,
    LSI_RSPHDR_WWW_AUTHENTICATE,
    LSI_RSPHDR_LITESPEED_CACHE,
    LSI_RSPHDR_LITESPEED_PURGE,
    LSI_RSPHDR_LITESPEED_TAG,
    LSI_RSPHDR_LITESPEED_VARY,
    LSI_RSPHDR_LSC_COOKIE,
    LSI_RSPHDR_X_POWERED_BY,
    LSI_RSPHDR_LINK,
    LSI_RSPHDR_VERSION,
    LSI_RSPHDR_ALT_SVC,
    LSI_RSPHDR_LITESPEED_ALT_SVC,
    LSI_RSPHDR_LSADC_BACKEND,
    LSI_RSPHDR_UPGRADE,
    LSI_RSPHDR_LITESPEED_PURGE2,
    LSI_RSPHDR_END,
    LSI_RSPHDR_UNKNOWN = LSI_RSPHDR_END
};

/* ------------------------------------------------------------------ */
/*  Request variable IDs                                               */
/* ------------------------------------------------------------------ */

enum {
    LSI_VAR_REMOTE_ADDR = 0,
    LSI_VAR_REMOTE_PORT,
    LSI_VAR_REMOTE_HOST,
    LSI_VAR_REMOTE_USER,
    LSI_VAR_REMOTE_IDENT,
    LSI_VAR_REQ_METHOD,
    LSI_VAR_QUERY_STRING,
    LSI_VAR_AUTH_TYPE,
    LSI_VAR_PATH_INFO,
    LSI_VAR_SCRIPTFILENAME,
    LSI_VAR_REQUST_FN,
    LSI_VAR_REQ_URI,
    LSI_VAR_DOC_ROOT
};

/* ------------------------------------------------------------------ */
/*  Opaque / forward-declared types                                    */
/* ------------------------------------------------------------------ */

typedef struct evtcbhead_s      lsi_session_t;
typedef struct lsi_module_s     lsi_module_t;
typedef struct lsi_param_s      lsi_param_t;
typedef struct lsi_hookinfo_s   lsi_hookchain_t;
typedef struct lsi_serverhook_s lsi_serverhook_t;
typedef struct lsi_reqhdlr_s    lsi_reqhdlr_t;
typedef struct lsi_confparser_s lsi_confparser_t;
typedef struct lsi_api_s        lsi_api_t;

/* ------------------------------------------------------------------ */
/*  Callback signatures                                                */
/* ------------------------------------------------------------------ */

typedef int (*lsi_callback_pf)(lsi_param_t *);
typedef int (*lsi_hook_cb)(lsi_session_t *session);
typedef int (*lsi_datarelease_pf)(void *);
typedef void (*lsi_timercb_pf)(const void *);

/* ------------------------------------------------------------------ */
/*  Server hook structure                                              */
/* ------------------------------------------------------------------ */

struct lsi_serverhook_s {
    int             index;
    lsi_callback_pf cb;
    short           priority;
    short           flag;
};

#define LSI_HOOK_END    {0, NULL, 0, 0}
#define LSI_FLAG_ENABLED 8

/* ------------------------------------------------------------------ */
/*  Module reserved size and descriptor                                */
/* ------------------------------------------------------------------ */

#define LSI_MODULE_RESERVED_SIZE    ((3 * sizeof(void *)) \
                                     + ((LSI_HKPT_TOTAL_COUNT + 2) * sizeof(int32_t)) \
                                     + (LSI_DATA_COUNT * sizeof(int16_t)))

#define MODULE_LOG_LEVEL(x)      (*(int32_t *)(x->reserved))

struct lsi_module_s {
    int64_t                  signature;
    int                    (*init_pf)(lsi_module_t *module);
    lsi_reqhdlr_t           *reqhandler;
    lsi_confparser_t        *config_parser;
    const char              *about;
    lsi_serverhook_t        *serverhook;
    int32_t                  reserved[(LSI_MODULE_RESERVED_SIZE + 3) / 4];
};

/* ------------------------------------------------------------------ */
/*  Minimal lsi_api_t stub (enough for lsiapi_shim.c to compile)       */
/* ------------------------------------------------------------------ */

struct lsi_param_s {
    const lsi_session_t      *session;
    const lsi_hookchain_t    *hook_chain;
    const void               *cur_hook;
    const void               *ptr1;
    int                       len1;
    int                      *flag_out;
    int                       flag_in;
};

struct lsi_api_s {
    const char *(*get_server_root)();
    void (*log)(const lsi_session_t *, int, const char *, ...);
    void (*vlog)(const lsi_session_t *, int, const char *, va_list, int);
    void (*lograw)(const lsi_session_t *, const char *, int);
    void *(*get_config)(const lsi_session_t *, const lsi_module_t *);
    int (*enable_hook)(const lsi_session_t *, const lsi_module_t *, int, int *, int);
    int (*get_hook_flag)(const lsi_session_t *, int);
    int (*get_hook_level)(lsi_param_t *);
    const lsi_module_t *(*get_module)(lsi_param_t *);
    int (*init_module_data)(const lsi_module_t *, lsi_datarelease_pf, int);
    int (*init_file_type_mdata)(const lsi_session_t *, const lsi_module_t *, const char *, int);
    int (*set_module_data)(const lsi_session_t *, const lsi_module_t *, int, void *);
    void *(*get_module_data)(const lsi_session_t *, const lsi_module_t *, int);
    void *(*get_cb_module_data)(const lsi_param_t *, int);
    void (*free_module_data)(const lsi_session_t *, const lsi_module_t *, int, lsi_datarelease_pf);
    int (*get_req_method)(const lsi_session_t *);
    const void *(*get_req_vhost)(const lsi_session_t *);
    int (*stream_writev_next)(lsi_param_t *, struct iovec *, int);
    int (*stream_read_next)(lsi_param_t *, char *, int);
    int (*stream_write_next)(lsi_param_t *, const char *, int);
    int (*get_req_raw_headers_length)(const lsi_session_t *);
    int (*get_req_raw_headers)(const lsi_session_t *, char *, int);
    int (*get_req_headers_count)(const lsi_session_t *);
    int (*get_req_headers)(const lsi_session_t *, struct iovec *, struct iovec *, int);
    const char *(*get_req_header)(const lsi_session_t *, const char *, int, int *);
    const char *(*get_req_header_by_id)(const lsi_session_t *, int, int *);
    int (*get_req_org_uri)(const lsi_session_t *, char *, int);
    const char *(*get_req_uri)(const lsi_session_t *, int *);
    const char *(*get_mapped_context_uri)(const lsi_session_t *, int *);
    int (*is_req_handler_registered)(const lsi_session_t *);
    int (*register_req_handler)(const lsi_session_t *, lsi_module_t *, int);
    int (*set_handler_write_state)(const lsi_session_t *, int);
    int (*set_timer)(unsigned int, int, lsi_timercb_pf, const void *);
    int (*remove_timer)(int);
    long (*create_event)(void *, const lsi_session_t *, long, void *, int);
    long (*create_session_resume_event)(const lsi_session_t *, lsi_module_t *);
    long (*get_event_obj)(void *, const lsi_session_t *, long, void *);
    void (*cancel_event)(const lsi_session_t *, long);
    void (*schedule_event)(long, int);
    long (*schedule_remove_session_cbs_event)(const lsi_session_t *);
    const char *(*get_req_cookies)(const lsi_session_t *, int *);
    int (*get_req_cookie_count)(const lsi_session_t *);
    const char *(*get_cookie_value)(const lsi_session_t *, const char *, int, int *);
    int (*get_cookie_by_index)(const lsi_session_t *, int, void *);
    const char *(*get_client_ip)(const lsi_session_t *, int *);
    const char *(*get_req_query_string)(const lsi_session_t *, int *);
    int (*get_req_var_by_id)(const lsi_session_t *, int, char *, int);
    int (*get_req_env)(const lsi_session_t *, const char *, unsigned int, char *, int);
    void (*set_req_env)(const lsi_session_t *, const char *, unsigned int, const char *, int);
    int (*register_env_handler)(const char *, unsigned int, lsi_callback_pf);
    int (*get_uri_file_path)(const lsi_session_t *, const char *, int, char *, int);
    int (*set_uri_qs)(const lsi_session_t *, int, const char *, int, const char *, int);
    int64_t (*get_req_content_length)(const lsi_session_t *);
    int (*read_req_body)(const lsi_session_t *, char *, int);
    int (*is_req_body_finished)(const lsi_session_t *);
    int (*set_req_wait_full_body)(const lsi_session_t *);
    int (*set_resp_wait_full_body)(const lsi_session_t *);
    int (*parse_req_args)(const lsi_session_t *, int, int, const char *, int);
    int (*get_req_args_count)(const lsi_session_t *);
    int (*get_req_arg_by_idx)(const lsi_session_t *, int, void *, char **);
    int (*get_qs_args_count)(const lsi_session_t *);
    int (*get_qs_arg_by_idx)(const lsi_session_t *, int, void *);
    int (*get_post_args_count)(const lsi_session_t *);
    int (*get_post_arg_by_idx)(const lsi_session_t *, int, void *, char **);
    int (*is_post_file_upload)(const lsi_session_t *, int);
    void (*set_status_code)(const lsi_session_t *, int);
    int (*get_status_code)(const lsi_session_t *);
    int (*is_resp_buffer_available)(const lsi_session_t *);
    int (*get_resp_buffer_compress_method)(const lsi_session_t *);
    int (*set_resp_buffer_compress_method)(const lsi_session_t *, int);
    int (*append_resp_body)(const lsi_session_t *, const char *, int);
    int (*append_resp_bodyv)(const lsi_session_t *, const struct iovec *, int);
    int (*send_file)(const lsi_session_t *, const char *, int64_t, int64_t);
    int (*send_file2)(const lsi_session_t *, int, int64_t, int64_t);
    int (*flush)(const lsi_session_t *);
    void (*end_resp)(const lsi_session_t *);
    int (*set_resp_content_length)(const lsi_session_t *, int64_t);
    int (*set_resp_header)(const lsi_session_t *, unsigned int, const char *, int, const char *, int, int);
    int (*set_resp_header2)(const lsi_session_t *, const char *, int, int);
    int (*set_resp_cookies)(const lsi_session_t *, const char *, const char *, const char *, const char *, int, int, int);
    int (*get_resp_header)(const lsi_session_t *, unsigned int, const char *, int, struct iovec *, int);
    int (*get_resp_headers_count)(const lsi_session_t *);
    unsigned int (*get_resp_header_id)(const lsi_session_t *, const char *);
    int (*get_resp_headers)(const lsi_session_t *, struct iovec *, struct iovec *, int);
    int (*remove_resp_header)(const lsi_session_t *, unsigned int, const char *, int);
    int (*get_file_path_by_uri)(const lsi_session_t *, const char *, int, char *, int);
    const char *(*get_mime_type_by_suffix)(const lsi_session_t *, const char *);
    int (*set_force_mime_type)(const lsi_session_t *, const char *);
    const char *(*get_req_file_path)(const lsi_session_t *, int *);
    const char *(*get_req_handler_type)(const lsi_session_t *);
    int (*is_access_log_on)(const lsi_session_t *);
    void (*set_access_log)(const lsi_session_t *, int);
    int (*get_access_log_string)(const lsi_session_t *, const char *, char *, int);
    int (*get_file_stat)(const lsi_session_t *, const char *, int, struct stat *);
    int (*is_resp_handler_aborted)(const lsi_session_t *);
    void *(*get_resp_body_buf)(const lsi_session_t *);
    void *(*get_req_body_buf)(const lsi_session_t *);
    void *(*get_new_body_buf)(int64_t);
    int64_t (*get_body_buf_size)(void *);
    int (*is_body_buf_eof)(void *, int64_t);
    const char *(*acquire_body_buf_block)(void *, int64_t, int *);
    void (*release_body_buf_block)(void *, int64_t);
    void (*reset_body_buf)(void *, int);
    int (*append_body_buf)(void *, const char *, int);
    int (*set_req_body_buf)(const lsi_session_t *, void *);
    int (*get_body_buf_fd)(void *);
    void (*send_resp_headers)(const lsi_session_t *);
    int (*is_resp_headers_sent)(const lsi_session_t *);
    const char *(*get_module_name)(const lsi_module_t *);
    void *(*get_multiplexer)();
    void *(*edio_reg)(int, void *, void *, short, void *);
    void (*edio_remove)(void *);
    void (*edio_modify)(void *, short, int);
    int (*get_client_access_level)(const lsi_session_t *);
    int (*is_suspended)(const lsi_session_t *);
    int (*resume)(const lsi_session_t *, int);
    int (*exec_ext_cmd)(const lsi_session_t *, const char *, int, void *, long, void *);
    char *(*get_ext_cmd_res_buf)(const lsi_session_t *, int *);
    long (*get_cur_time)(int32_t *);
    int (*get_vhost_count)();
    const void *(*get_vhost)(int);
    int (*set_vhost_module_data)(const void *, const lsi_module_t *, void *);
    void *(*get_vhost_module_data)(const void *, const lsi_module_t *);
    void *(*get_vhost_module_conf)(const void *, const lsi_module_t *);
    void *(*get_session_pool)(const lsi_session_t *);
    int (*get_local_sockaddr)(const lsi_session_t *, char *, int);
    int (*handoff_fd)(const lsi_session_t *, char **, int *);
    int (*expand_current_server_variable)(int, const char *, char *, int);
    void (*module_log)(const lsi_module_t *, const lsi_session_t *, int, const char *, ...);
    void (*c_log)(const char *, const lsi_session_t *, int, const char *, ...);
    const int *_log_level_ptr;
    int (*set_ua_code)(const char *, char *);
    char *(*get_ua_code)(const char *);
    void (*foreach)(const lsi_session_t *, int, const char *, void *, void *);
    void (*register_thread_cleanup)(const lsi_module_t *, void (*)(void *), void *);

    /* PHPConfig extensions — appended at end for ABI compatibility.
     * These are NULL in stock OLS; populated in CyberPanel's custom OLS.
     * litehttpd checks for NULL at runtime and falls back to env vars.
     *
     * API matches CyberPanel's custom OLS:
     *   type: 2=PHP_INI_PERDIR (php_value/php_flag),
     *         4=PHP_INI_SYSTEM (php_admin_value/php_admin_flag) */
    int (*set_php_config_value)(const lsi_session_t *session,
                                const char *name, const char *value,
                                int type);
    int (*set_php_config_flag)(const lsi_session_t *session,
                               const char *name, int value,
                               int type);
    int (*get_php_config)(const lsi_session_t *session,
                          const char *name, char *buf, int buf_len);
    int (*set_req_header)(const lsi_session_t *session,
                          const char *name, int name_len,
                          const char *value, int value_len,
                          int op);

    /* Rewrite engine API (requires custom OLS with rewrite patch) */
    void *(*parse_rewrite_rules)(const char *rules_text, int text_len);
    int (*exec_rewrite_rules)(const lsi_session_t *session, void *handle,
                              const char *base, int base_len);
    void (*free_rewrite_rules)(void *handle);
};

extern __thread const lsi_api_t *g_api;

/* ------------------------------------------------------------------ */
/*  LSIAPI function declarations                                       */
/*  Implemented by lsiapi_shim.c (production) or mock_lsiapi.cpp (test)*/
/* ------------------------------------------------------------------ */

/* Request headers */
const char *lsi_session_get_req_header_by_name(lsi_session_t *session,
                                                const char *name, int name_len,
                                                int *val_len);
int         lsi_session_set_req_header(lsi_session_t *session,
                                       const char *name, int name_len,
                                       const char *val, int val_len);
int         lsi_session_remove_req_header(lsi_session_t *session,
                                          const char *name, int name_len);

/* Response headers */
const char *lsi_session_get_resp_header_by_name(lsi_session_t *session,
                                                 const char *name, int name_len,
                                                 int *val_len);
int         lsi_session_set_resp_header(lsi_session_t *session,
                                        const char *name, int name_len,
                                        const char *val, int val_len);
int         lsi_session_set_resp_content_type(lsi_session_t *session,
                                               const char *val, int val_len);
const char *lsi_session_get_resp_content_type(lsi_session_t *session,
                                               int *val_len);
int         lsi_session_add_resp_header(lsi_session_t *session,
                                        const char *name, int name_len,
                                        const char *val, int val_len);
int         lsi_session_append_resp_header(lsi_session_t *session,
                                           const char *name, int name_len,
                                           const char *val, int val_len);
int         lsi_session_remove_resp_header(lsi_session_t *session,
                                           const char *name, int name_len);
int         lsi_session_get_resp_header_count(lsi_session_t *session,
                                               const char *name, int name_len);

/* Environment variables */
const char *lsi_session_get_env(lsi_session_t *session,
                                const char *name, int name_len, int *val_len);
int         lsi_session_set_env(lsi_session_t *session,
                                const char *name, int name_len,
                                const char *val, int val_len);

/* Response status */
int         lsi_session_get_status(lsi_session_t *session);
int         lsi_session_set_status(lsi_session_t *session, int code);

/* End response — tell OLS to finalize and send the response */
void        lsi_session_end_resp(lsi_session_t *session);

/* Request URI */
const char *lsi_session_get_uri(lsi_session_t *session, int *uri_len);

/* Document root */
const char *lsi_session_get_doc_root(lsi_session_t *session, int *len);

/* Client IP */
const char *lsi_session_get_client_ip(lsi_session_t *session, int *len);

/* URL redirect action codes for g_api->set_uri_qs() */
#define LSI_URL_REDIRECT_INTERNAL  2
#define LSI_URL_REDIRECT_301       3
#define LSI_URL_REDIRECT_302       4
#define LSI_URL_REDIRECT_303       5
#define LSI_URL_REDIRECT_307       6

/* External redirect via g_api->set_uri_qs — works in URI_MAP phase */
int         lsi_session_redirect(lsi_session_t *session,
                                 int status_code,
                                 const char *url, int url_len);

/* PHP configuration
 * type: 0 = php_value (PHP_INI_PERDIR),
 *       1 = php_flag (PHP_INI_PERDIR),
 *       2 = php_admin_value (PHP_INI_SYSTEM),
 *       3 = php_admin_flag (PHP_INI_SYSTEM)  */
#define PHP_INI_TYPE_VALUE       0
#define PHP_INI_TYPE_FLAG        1
#define PHP_INI_TYPE_ADMIN_VALUE 2
#define PHP_INI_TYPE_ADMIN_FLAG  3
int         lsi_session_set_php_ini(lsi_session_t *session,
                                    const char *name, const char *val,
                                    int type);

/* Response body */
int         lsi_session_set_resp_body(lsi_session_t *session,
                                      const char *buf, int len);

/* Hook registration */
int         lsi_register_hook(int hook_point, lsi_hook_cb cb, int priority);

/* Logging */
void        lsi_log(lsi_session_t *session, int level, const char *fmt, ...);

/* v2 extensions */
int         lsi_session_set_dir_option(lsi_session_t *session,
                                       const char *option, int enabled);
int         lsi_session_get_dir_option(lsi_session_t *session,
                                       const char *option);
int         lsi_session_set_uri_internal(lsi_session_t *session,
                                         const char *uri, int uri_len);
int         lsi_session_file_exists(lsi_session_t *session, const char *path);
const char *lsi_session_get_method(lsi_session_t *session, int *len);
const char *lsi_session_get_auth_header(lsi_session_t *session, int *len);
const char *lsi_session_get_query_string(lsi_session_t *session, int *len);
int         lsi_session_set_www_authenticate(lsi_session_t *session,
                                             const char *realm, int realm_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LS_H */
