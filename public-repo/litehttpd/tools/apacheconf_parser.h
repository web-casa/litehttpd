/*
 * apacheconf_parser.h -- Apache httpd.conf parser data structures
 *
 * Part of litehttpd-confconv: Apache -> OpenLiteSpeed config converter.
 */
#ifndef APACHECONF_PARSER_H
#define APACHECONF_PARSER_H

#include <stdio.h>

#define AP_MAX_VHOSTS       256
#define AP_MAX_CONTEXTS     64
#define AP_MAX_LISTEN       32
#define AP_MAX_INCLUDE_DEPTH 16
#define AP_MAX_LINE         4096
#define AP_MAX_FILE_SIZE    (10 * 1024 * 1024)  /* 10 MB */
#define AP_MAX_PORTMAP      16
#define AP_MAX_ENV_VARS     64
#define AP_MAX_MACROS       64
#define AP_MAX_MACRO_PARAMS 16
#define AP_MAX_MACRO_BODY   (64 * 1024)

/* Directory/Location context */
typedef struct ap_context {
    char *type;          /* "directory", "location", "locationmatch" */
    char *path;
    char *options;       /* Options directive value */
    char *allow_override;
    char *dir_index;
    char *handler;       /* SetHandler value */
    int   allow_browse;  /* 1 = +Indexes, 0 = -Indexes, -1 = unset */
    int   follow_symlinks; /* 1, 0, or -1 unset */
    /* Access control */
    char *access_allow;  /* "Allow from ..." value */
    char *access_deny;   /* "Deny from ..." value */
    int   order;         /* 0=allow,deny  1=deny,allow */
    int   require_all;   /* 1=granted, 0=denied, -1=unset */
} ap_context_t;

/* VirtualHost */
typedef struct ap_vhost {
    char *server_name;
    char *server_aliases;   /* space-separated */
    char *server_admin;
    char *doc_root;
    int   listen_port;
    int   listen_ssl;
    /* SSL */
    char *ssl_cert;
    char *ssl_key;
    char *ssl_chain;
    /* PHP */
    char *php_handler;      /* detected lsphp handler hint */
    /* Rewrite */
    int   rewrite_enabled;
    char *rewrite_rules;    /* accumulated RewriteCond/Rule text */
    /* Headers */
    char *extra_headers;    /* Header/RequestHeader lines */
    /* Error pages */
    char *error_pages;      /* ErrorDocument lines */
    /* Redirect rules */
    char *redirect_rules;   /* Redirect/RedirectMatch lines */
    /* Contexts */
    ap_context_t contexts[AP_MAX_CONTEXTS];
    int context_count;
    /* Global-level directives within vhost */
    char *allow_override;
    char *dir_index;
    char *options;
    /* Alias mappings: stored as "uri path" pairs separated by newlines */
    char *aliases;
    /* PHP INI overrides */
    char *php_values;           /* php_value name=value pairs (\n separated) */
    char *php_flags;            /* php_flag name=value pairs */
    char *php_admin_values;     /* php_admin_value pairs */
    char *php_admin_flags;      /* php_admin_flag pairs */
    /* SSL extended */
    char *ssl_protocol;         /* SSLProtocol string (e.g., "TLSv1.2 TLSv1.3") */
    char *ssl_cipher_suite;     /* SSLCipherSuite string */
    int   ssl_stapling;         /* SSLUseStapling On=1 */
    /* Proxy */
    char *proxy_pass;           /* ProxyPass entries (\n separated "path backend") */
    char *proxy_pass_reverse;   /* ProxyPassReverse entries */
    /* Expires */
    char *expires_by_type;      /* ExpiresByType entries (\n separated "type=Aseconds") */
    int   expires_active;       /* ExpiresActive On=1 */
    /* Suexec */
    char *suexec_user;          /* SuexecUserGroup user */
    char *suexec_group;         /* SuexecUserGroup group */
    /* ModSecurity */
    int   modsecurity_enabled;  /* SecRuleEngine On=1, DetectionOnly=2 */
    /* Limits */
    long  req_body_limit;       /* LimitRequestBody bytes */
    /* Environment variables: "name value" pairs (\n separated) */
    char *env_vars;
    /* SSL extended (CA/OCSP/verify) */
    char *ssl_ca_cert_file;     /* SSLCACertificateFile */
    char *ssl_ca_cert_path;     /* SSLCACertificatePath */
    char *ssl_ca_rev_file;      /* SSLCARevocationFile */
    char *ssl_ocsp_responder;   /* SSLOCSPDefaultResponder */
    int   ssl_verify_client;    /* 0=none, 1=optional, 2=require */
    int   ssl_verify_depth;     /* SSLVerifyDepth (default 1) */
    int   ssl_session_tickets;  /* SSLSessionTickets On=1, Off=0, -1=unset */
} ap_vhost_t;

/* Panel detection */
typedef enum {
    CP_NONE = 0,
    CP_DIRECTADMIN = 1,
    CP_INTERWORX = 2,
    CP_CYBERCP = 3
} panel_type_t;

/* Macro definition (<Macro> / Use) */
typedef struct ap_macro {
    char *name;
    char *params[AP_MAX_MACRO_PARAMS];   /* $param or @param names */
    int   param_count;
    char *body;                           /* accumulated body text */
    int   body_len;
} ap_macro_t;

/* Top-level config */
typedef struct ap_config {
    ap_vhost_t vhosts[AP_MAX_VHOSTS];
    int vhost_count;
    int listen_ports[AP_MAX_LISTEN];
    int listen_ssl[AP_MAX_LISTEN];     /* 1 if this port is SSL */
    int listen_count;
    /* Port mapping (Apache port -> OLS port) */
    int port_from[AP_MAX_PORTMAP];
    int port_to[AP_MAX_PORTMAP];
    int port_map_count;
    /* Global defaults */
    char *server_root;
    char *server_name;
    char *server_admin;
    char *doc_root;
    char *user;
    char *group;
    /* Panel detection */
    panel_type_t panel_type;
    /* Macro definitions */
    ap_macro_t macros[AP_MAX_MACROS];
    int macro_count;
} ap_config_t;

/* Detect hosting control panel.
 * config_path may be NULL; when provided, restricts detection to panels
 * whose expected config directories match the path prefix.
 */
panel_type_t ap_detect_panel(const char *config_path);

/* Parse Apache expires duration string to seconds.
 * Input: "access plus 7 days", "access plus 1 hour", etc.
 * Returns: seconds (0 on parse failure).
 */
long ap_parse_expires_duration(const char *duration);

/* Parse Apache config file into ap_config_t. Returns 0 on success. */
int ap_parse_config(const char *path, ap_config_t *config);

/* Check if Apache config file has changed since last state save.
 * Returns: 1 if changed, 0 if unchanged, -1 on error.
 * Also updates the state file on change detection.
 */
int ap_check_config_changed(const char *config_path, const char *state_file);

/* Check if config has changed WITHOUT updating state file.
 * Returns: 1 if changed, 0 if unchanged, -1 on error.
 */
int ap_check_config_changed_no_save(const char *config_path,
                                    const char *state_file);

/* Update state file to reflect current config metadata.
 * Returns: 0 on success, -1 on error.
 */
int ap_save_config_state(const char *config_path, const char *state_file);

/* Free all memory in config. */
void ap_config_free(ap_config_t *config);

#endif /* APACHECONF_PARSER_H */
