/*
 * ols_config_writer.c -- OLS config generator
 *
 * Converts parsed Apache config (ap_config_t) to OpenLiteSpeed format.
 */
#include "ols_config_writer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- helpers ---- */

/* Sanitize a server name for use as a filesystem directory component.
 * Replaces '/', '\', and ".." sequences with '_'.
 * Returns 0 on success, -1 if name is empty or starts with '/'.
 * Output is written to 'out' (must be at least 'outlen' bytes).
 */
static int sanitize_server_name(const char *name, char *out, size_t outlen) {
    if (!name || !name[0]) return -1;
    if (name[0] == '/') return -1;  /* reject absolute paths */

    size_t len = strlen(name);
    if (len >= outlen) len = outlen - 1;
    memcpy(out, name, len);
    out[len] = '\0';

    /* Replace dangerous characters */
    for (size_t i = 0; i < len; i++) {
        if (out[i] == '/' || out[i] == '\\')
            out[i] = '_';
    }
    /* Replace ".." sequences */
    for (size_t i = 0; i + 1 < len; i++) {
        if (out[i] == '.' && out[i + 1] == '.') {
            out[i] = '_';
            out[i + 1] = '_';
        }
    }
    /* Final check: reject if empty after sanitization */
    if (!out[0]) return -1;
    return 0;
}

static int mkdirs(const char *path) {
    char buf[4096];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
    }
    return mkdir(buf, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

/* Map Apache AllowOverride string to OLS numeric value.
 * OLS uses a bitmask: 255 = All, 0 = None.
 */
static int map_allow_override(const char *value) {
    if (!value) return 0; /* default to None (secure default) */
    if (strcasecmp(value, "None") == 0) return 0;
    if (strcasecmp(value, "All") == 0) return 255;
    /* Partial: AuthConfig=1, FileInfo=2, Indexes=4, Limit=8, Options=16 */
    int result = 0;
    if (strcasestr(value, "AuthConfig")) result |= 1;
    if (strcasestr(value, "FileInfo"))   result |= 2;
    if (strcasestr(value, "Indexes"))    result |= 4;
    if (strcasestr(value, "Limit"))      result |= 8;
    if (strcasestr(value, "Options"))    result |= 16;
    return result;
}

/* Map Apache SSLProtocol string to OLS bitmask.
 * OLS bits: 1=SSLv3(obsolete), 2=TLSv1.0, 4=TLSv1.1, 8=TLSv1.2, 16=TLSv1.3
 * Default: 24 (TLSv1.2 + TLSv1.3)
 */
int ols_map_ssl_protocol(const char *proto) {
    if (!proto || !proto[0]) return 24; /* default: TLSv1.2 + TLSv1.3 */

    int bits = 0;
    int had_explicit_add = 0;
    char buf[AP_MAX_LINE];
    strncpy(buf, proto, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    char *tok = strtok_r(buf, " \t", &saveptr);
    while (tok) {
        int negative = 0;
        const char *name = tok;
        if (*name == '+') { name++; had_explicit_add = 1; }
        else if (*name == '-') { name++; negative = 1; }
        else { had_explicit_add = 1; }

        int bit = 0;
        if (strcasecmp(name, "all") == 0) bit = 2 | 4 | 8 | 16;
        else if (strcasecmp(name, "SSLv3") == 0) bit = 1;
        else if (strcasecmp(name, "TLSv1") == 0) bit = 2;
        else if (strcasecmp(name, "TLSv1.0") == 0) bit = 2;
        else if (strcasecmp(name, "TLSv1.1") == 0) bit = 4;
        else if (strcasecmp(name, "TLSv1.2") == 0) bit = 8;
        else if (strcasecmp(name, "TLSv1.3") == 0) bit = 16;

        if (negative) {
            if (!bits && !had_explicit_add) bits = 2 | 4 | 8 | 16; /* start from all */
            bits &= ~bit;
        } else {
            bits |= bit;
        }

        tok = strtok_r(NULL, " \t", &saveptr);
    }

    return bits ? bits : 24;
}

/* ---- PHP detection ---- */

const char *ols_detect_php(const char *handler_hint) {
    static char path_buf[256];
    /* Try to extract PHP version from handler hint */
    int versions[] = { 84, 83, 82, 81, 80 };
    int target_ver = 0;

    if (handler_hint) {
        /* Look for version patterns like "php8.3", "php83", "ea-php83" */
        const char *p = handler_hint;
        while (*p) {
            if ((*p == 'p' || *p == 'P') &&
                (p[1] == 'h' || p[1] == 'H') &&
                (p[2] == 'p' || p[2] == 'P')) {
                p += 3;
                /* Skip optional '-' */
                if (*p == '-') p++;
                int major = 0, minor = 0;
                if (*p >= '0' && *p <= '9') {
                    major = *p - '0'; p++;
                    if (*p == '.') p++;
                    if (*p >= '0' && *p <= '9') {
                        minor = *p - '0';
                    }
                    target_ver = major * 10 + minor;
                }
                break;
            }
            p++;
        }
    }

    /* Check for specific version first */
    if (target_ver > 0) {
        snprintf(path_buf, sizeof(path_buf),
                 "/usr/local/lsws/lsphp%d/bin/lsphp", target_ver);
        if (access(path_buf, X_OK) == 0)
            return path_buf;
    }

    /* Fall back: scan available versions */
    for (int i = 0; i < 5; i++) {
        snprintf(path_buf, sizeof(path_buf),
                 "/usr/local/lsws/lsphp%d/bin/lsphp", versions[i]);
        if (access(path_buf, X_OK) == 0)
            return path_buf;
    }

    return NULL;
}

/* ---- write listener block ---- */

static int ols_write_listener(FILE *fp, int port, int ssl, int mapped_port,
                              const char *cert, const char *key,
                              const char *chain) {
    fprintf(fp, "listener %s%d {\n",
            ssl ? "SSL" : "Default", port);
    fprintf(fp, "  address                 *:%d\n", mapped_port);
    fprintf(fp, "  secure                  %d\n", ssl ? 1 : 0);
    if (ssl && cert) {
        fprintf(fp, "  keyFile                 %s\n", key ? key : "");
        fprintf(fp, "  certFile                %s\n", cert);
        if (chain)
            fprintf(fp, "  certChain               1\n");
    }
    fprintf(fp, "}\n\n");
    return 0;
}

/* ---- write context block ---- */

static void ols_write_context(FILE *fp, const ap_context_t *ctx,
                              const char *doc_root) {
    const char *uri = ctx->path;
    if (!uri || !uri[0]) return;

    /* Map Directory path to URI context */
    if (strcasecmp(ctx->type, "directory") == 0) {
        /* If path starts with doc_root, make it relative */
        if (doc_root && strncmp(uri, doc_root, strlen(doc_root)) == 0) {
            uri = uri + strlen(doc_root);
            if (!uri[0]) uri = "/";
        }
        fprintf(fp, "\ncontext %s {\n", uri);
        fprintf(fp, "  type                    NULL\n");
        fprintf(fp, "  location                %s\n", ctx->path);
        fprintf(fp, "  allowBrowse             %d\n",
                ctx->allow_browse >= 0 ? ctx->allow_browse : 1);
    } else if (strcasecmp(ctx->type, "locationmatch") == 0) {
        fprintf(fp, "\ncontext exp:%s {\n", uri);
        fprintf(fp, "  type                    NULL\n");
        if (doc_root)
            fprintf(fp, "  location                %s\n", doc_root);
        fprintf(fp, "  allowBrowse             1\n");
    } else {
        /* Location */
        fprintf(fp, "\ncontext %s {\n", uri);
        fprintf(fp, "  type                    NULL\n");
        if (doc_root)
            fprintf(fp, "  location                %s%s\n", doc_root, uri);
        fprintf(fp, "  allowBrowse             1\n");
    }

    if (ctx->dir_index)
        fprintf(fp, "  indexFiles              %s\n", ctx->dir_index);
    if (ctx->allow_override)
        fprintf(fp, "  allowOverride           %d\n",
                map_allow_override(ctx->allow_override));

    /* Access control */
    if (ctx->require_all == 0) {
        fprintf(fp, "  accessControl {\n");
        fprintf(fp, "    deny                  *\n");
        fprintf(fp, "  }\n");
    } else if (ctx->access_deny || ctx->access_allow) {
        fprintf(fp, "  accessControl {\n");
        if (ctx->access_allow)
            fprintf(fp, "    allow                 %s\n", ctx->access_allow);
        if (ctx->access_deny)
            fprintf(fp, "    deny                  %s\n", ctx->access_deny);
        fprintf(fp, "  }\n");
    }

    fprintf(fp, "}\n");
}

/* ---- write single vhost config ---- */

int ols_write_vhost(const ap_vhost_t *vhost, const char *filepath) {
    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        fprintf(stderr, "litehttpd-confconv: cannot create %s: %s\n",
                filepath, strerror(errno));
        return -1;
    }

    const char *doc_root = vhost->doc_root;
    if (!doc_root) doc_root = "/var/www/html";

    fprintf(fp, "docRoot                   %s\n", doc_root);

    int ao = map_allow_override(vhost->allow_override);
    fprintf(fp, "enableRewrite             %d\n",
            vhost->rewrite_enabled ? 1 : 0);
    /* Only emit allowOverride/autoLoadHtaccess when explicitly configured */
    if (vhost->allow_override) {
        fprintf(fp, "allowOverride             %d\n", ao);
        fprintf(fp, "autoLoadHtaccess          1\n");
    }

    if (vhost->dir_index)
        fprintf(fp, "indexFiles                %s\n", vhost->dir_index);

    /* Auto index (from Options) */
    if (vhost->options) {
        if (strcasestr(vhost->options, "+Indexes") ||
            (strcasestr(vhost->options, "Indexes") &&
             !strcasestr(vhost->options, "-Indexes")))
            fprintf(fp, "autoIndex                 1\n");
        else
            fprintf(fp, "autoIndex                 0\n");
    } else {
        fprintf(fp, "autoIndex                 0\n");
    }

    /* Rewrite rules */
    if (vhost->rewrite_enabled && vhost->rewrite_rules) {
        fprintf(fp, "\nrewrite {\n");
        fprintf(fp, "  enable                  1\n");
        fprintf(fp, "  rules                   <<<END_RULES\n");
        fprintf(fp, "%s\n", vhost->rewrite_rules);

        /* Fix 4: Append redirect rules as rewrite rules */
        if (vhost->redirect_rules) {
            /* Convert Redirect/RedirectMatch to RewriteRule */
            char *redirs = strdup(vhost->redirect_rules);
            if (!redirs) { fclose(fp); return -1; }
            char *saveptr = NULL;
            char *rline = strtok_r(redirs, "\n", &saveptr);
            while (rline) {
                char directive[64] = {0};
                char arg1[AP_MAX_LINE] = {0};
                char arg2[AP_MAX_LINE] = {0};
                char arg3[AP_MAX_LINE] = {0};
                int nargs = sscanf(rline, "%63s %4095s %4095s %4095s",
                                   directive, arg1, arg2, arg3);
                if (nargs >= 3 && strcasecmp(directive, "Redirect") == 0) {
                    /* Redirect [status] from to */
                    int status = atoi(arg1);
                    if (status >= 300 && status < 400 && nargs >= 4) {
                        fprintf(fp, "RewriteRule ^%s$ %s [R=%d,L]\n",
                                arg2, arg3, status);
                    } else {
                        /* Redirect from to (default 302) */
                        fprintf(fp, "RewriteRule ^%s$ %s [R=302,L]\n",
                                arg1, arg2);
                    }
                } else if (nargs >= 3 &&
                           strcasecmp(directive, "RedirectMatch") == 0) {
                    /* RedirectMatch [status] pattern target */
                    int status = atoi(arg1);
                    if (status >= 300 && status < 400 && nargs >= 4) {
                        fprintf(fp, "RewriteRule %s %s [R=%d,L]\n",
                                arg2, arg3, status);
                    } else {
                        fprintf(fp, "RewriteRule %s %s [R=302,L]\n",
                                arg1, arg2);
                    }
                }
                rline = strtok_r(NULL, "\n", &saveptr);
            }
            free(redirs);
        }

        fprintf(fp, "END_RULES\n");
        fprintf(fp, "}\n");
    } else if (vhost->redirect_rules) {
        /* Redirects without rewrite engine: enable rewrite for them */
        fprintf(fp, "\nrewrite {\n");
        fprintf(fp, "  enable                  1\n");
        fprintf(fp, "  rules                   <<<END_RULES\n");

        char *redirs = strdup(vhost->redirect_rules);
        if (!redirs) { fclose(fp); return -1; }
        char *saveptr = NULL;
        char *rline = strtok_r(redirs, "\n", &saveptr);
        while (rline) {
            char directive[64] = {0};
            char arg1[AP_MAX_LINE] = {0};
            char arg2[AP_MAX_LINE] = {0};
            char arg3[AP_MAX_LINE] = {0};
            int nargs = sscanf(rline, "%63s %4095s %4095s %4095s",
                               directive, arg1, arg2, arg3);
            if (nargs >= 3 && strcasecmp(directive, "Redirect") == 0) {
                int status = atoi(arg1);
                if (status >= 300 && status < 400 && nargs >= 4) {
                    fprintf(fp, "RewriteRule ^%s$ %s [R=%d,L]\n",
                            arg2, arg3, status);
                } else {
                    fprintf(fp, "RewriteRule ^%s$ %s [R=302,L]\n",
                            arg1, arg2);
                }
            } else if (nargs >= 3 &&
                       strcasecmp(directive, "RedirectMatch") == 0) {
                int status = atoi(arg1);
                if (status >= 300 && status < 400 && nargs >= 4) {
                    fprintf(fp, "RewriteRule %s %s [R=%d,L]\n",
                            arg2, arg3, status);
                } else {
                    fprintf(fp, "RewriteRule %s %s [R=302,L]\n",
                            arg1, arg2);
                }
            }
            rline = strtok_r(NULL, "\n", &saveptr);
        }
        free(redirs);

        fprintf(fp, "END_RULES\n");
        fprintf(fp, "}\n");
    }

    /* PHP detection and extprocessor block */
    if (vhost->php_handler) {
        const char *lsphp = ols_detect_php(vhost->php_handler);
        if (lsphp) {
            /* Extract version number from path like /usr/.../lsphp83/bin/lsphp */
            int php_ver = 0;
            const char *ver_p = strstr(lsphp, "lsphp");
            if (ver_p) {
                ver_p += 5; /* skip "lsphp" */
                if (*ver_p >= '0' && *ver_p <= '9') {
                    php_ver = atoi(ver_p);
                }
            }
            if (php_ver > 0) {
                fprintf(fp, "\nextprocessor lsphp%d {\n", php_ver);
                fprintf(fp, "  type                    lsapi\n");
                fprintf(fp, "  address                 uds://tmp/lshttpd/lsphp%d.sock\n",
                        php_ver);
                fprintf(fp, "  maxConns                10\n");
                fprintf(fp, "  env                     PHP_LSAPI_CHILDREN=10\n");
                fprintf(fp, "  initTimeout             60\n");
                fprintf(fp, "  retryTimeout            0\n");
                fprintf(fp, "  path                    %s\n", lsphp);
                fprintf(fp, "  backlog                 100\n");
                fprintf(fp, "  instances               1\n");
                fprintf(fp, "}\n");
                fprintf(fp, "\nscripthandler {\n");
                fprintf(fp, "  add                     lsapi:lsphp%d php\n",
                        php_ver);
                fprintf(fp, "}\n");
            }
        }
    }

    /* SSL extended settings */
    if (vhost->listen_ssl) {
        if (vhost->ssl_protocol) {
            int proto_bits = ols_map_ssl_protocol(vhost->ssl_protocol);
            fprintf(fp, "sslProtocol               %d\n", proto_bits);
        }
        if (vhost->ssl_cipher_suite)
            fprintf(fp, "sslCipherSuite            %s\n",
                    vhost->ssl_cipher_suite);
        if (vhost->ssl_stapling)
            fprintf(fp, "enableStapling            1\n");
    }

    /* PHP INI overrides */
    if (vhost->php_values || vhost->php_flags ||
        vhost->php_admin_values || vhost->php_admin_flags) {
        fprintf(fp, "\nphpIniOverride {\n");
        if (vhost->php_values) {
            char *vals = strdup(vhost->php_values);
            if (!vals) { fclose(fp); return -1; }
            char *saveptr = NULL;
            char *line = strtok_r(vals, "\n", &saveptr);
            while (line) {
                fprintf(fp, "  php_value %s\n", line);
                line = strtok_r(NULL, "\n", &saveptr);
            }
            free(vals);
        }
        if (vhost->php_flags) {
            char *vals = strdup(vhost->php_flags);
            if (!vals) { fclose(fp); return -1; }
            char *saveptr = NULL;
            char *line = strtok_r(vals, "\n", &saveptr);
            while (line) {
                fprintf(fp, "  php_flag %s\n", line);
                line = strtok_r(NULL, "\n", &saveptr);
            }
            free(vals);
        }
        if (vhost->php_admin_values) {
            char *vals = strdup(vhost->php_admin_values);
            if (!vals) { fclose(fp); return -1; }
            char *saveptr = NULL;
            char *line = strtok_r(vals, "\n", &saveptr);
            while (line) {
                fprintf(fp, "  php_admin_value %s\n", line);
                line = strtok_r(NULL, "\n", &saveptr);
            }
            free(vals);
        }
        if (vhost->php_admin_flags) {
            char *vals = strdup(vhost->php_admin_flags);
            if (!vals) { fclose(fp); return -1; }
            char *saveptr = NULL;
            char *line = strtok_r(vals, "\n", &saveptr);
            while (line) {
                fprintf(fp, "  php_admin_flag %s\n", line);
                line = strtok_r(NULL, "\n", &saveptr);
            }
            free(vals);
        }
        fprintf(fp, "}\n");
    }

    /* Proxy backends */
    if (vhost->proxy_pass) {
        char *proxies = strdup(vhost->proxy_pass);
        if (!proxies) { fclose(fp); return -1; }
        char *saveptr = NULL;
        char *line = strtok_r(proxies, "\n", &saveptr);
        int proxy_idx = 0;
        while (line) {
            char path[AP_MAX_LINE] = {0};
            char backend[AP_MAX_LINE] = {0};
            if (sscanf(line, "%4095s %4095s", path, backend) == 2) {
                fprintf(fp, "\nextprocessor proxy_backend_%d {\n", proxy_idx);
                fprintf(fp, "  type                    proxy\n");
                fprintf(fp, "  address                 %s\n", backend);
                fprintf(fp, "  maxConns                100\n");
                fprintf(fp, "}\n");
                fprintf(fp, "\ncontext %s {\n", path);
                fprintf(fp, "  type                    proxy\n");
                fprintf(fp, "  handler                 proxy_backend_%d\n",
                        proxy_idx);
                fprintf(fp, "  addDefaultCharset       off\n");
                fprintf(fp, "}\n");
                proxy_idx++;
            }
            line = strtok_r(NULL, "\n", &saveptr);
        }
        free(proxies);
    }

    /* Expires */
    if (vhost->expires_active || vhost->expires_by_type) {
        fprintf(fp, "\nexpires {\n");
        fprintf(fp, "  enableExpires           %d\n",
                vhost->expires_active ? 1 : 0);
        if (vhost->expires_by_type) {
            char *types = strdup(vhost->expires_by_type);
            if (!types) { fclose(fp); return -1; }
            char *saveptr = NULL;
            char *line = strtok_r(types, "\n", &saveptr);
            while (line) {
                fprintf(fp, "  expiresByType           %s\n", line);
                line = strtok_r(NULL, "\n", &saveptr);
            }
            free(types);
        }
        fprintf(fp, "}\n");
    }

    /* Suexec */
    if (vhost->suexec_user || vhost->suexec_group) {
        fprintf(fp, "setUIDMode                2\n");
        fprintf(fp, "chrootMode                0\n");
        if (vhost->suexec_user)
            fprintf(fp, "user                      %s\n", vhost->suexec_user);
        if (vhost->suexec_group)
            fprintf(fp, "group                     %s\n", vhost->suexec_group);
    }

    /* ModSecurity */
    if (vhost->modsecurity_enabled > 0) {
        fprintf(fp, "\n# ModSecurity: SecRuleEngine %s\n",
                vhost->modsecurity_enabled == 2 ? "DetectionOnly" : "On");
        fprintf(fp, "# Requires mod_security module to be loaded in OLS\n");
    }

    /* LimitRequestBody */
    if (vhost->req_body_limit > 0) {
        fprintf(fp, "reqBodyLimit              %ld\n", vhost->req_body_limit);
    }

    /* Extra headers */
    if (vhost->extra_headers) {
        fprintf(fp, "\nmodule litehttpd_htaccess {\n");
        fprintf(fp, "  ls_enabled              1\n");
        fprintf(fp, "}\n");
    }

    /* Error pages */
    if (vhost->error_pages) {
        char *pages = strdup(vhost->error_pages);
        if (!pages) { fclose(fp); return -1; }
        char *saveptr = NULL;
        char *line = strtok_r(pages, "\n", &saveptr);
        while (line) {
            int code = 0;
            char url[AP_MAX_LINE] = {0};
            if (sscanf(line, "%d %4095s", &code, url) == 2 && code >= 400) {
                fprintf(fp, "\nerrorPage %d {\n", code);
                fprintf(fp, "  url                     %s\n", url);
                fprintf(fp, "}\n");
            }
            line = strtok_r(NULL, "\n", &saveptr);
        }
        free(pages);
    }

    /* Default root context */
    fprintf(fp, "\ncontext / {\n");
    fprintf(fp, "  allowBrowse             1\n");
    fprintf(fp, "  location                %s\n", doc_root);
    if (vhost->rewrite_enabled) {
        fprintf(fp, "  rewrite {\n");
        fprintf(fp, "    enable                1\n");
        fprintf(fp, "  }\n");
    }
    fprintf(fp, "}\n");

    /* Additional contexts from <Directory>/<Location> blocks */
    for (int i = 0; i < vhost->context_count; i++) {
        ols_write_context(fp, &vhost->contexts[i], doc_root);
    }

    /* Alias contexts */
    if (vhost->aliases) {
        char *aliases = strdup(vhost->aliases);
        if (!aliases) { fclose(fp); return -1; }
        char *saveptr = NULL;
        char *line = strtok_r(aliases, "\n", &saveptr);
        while (line) {
            char uri[AP_MAX_LINE] = {0};
            char path[AP_MAX_LINE] = {0};
            if (sscanf(line, "%4095s %4095s", uri, path) == 2) {
                /* Strip surrounding quotes */
                size_t ulen = strlen(uri);
                if (ulen >= 2 && uri[0] == '"' && uri[ulen-1] == '"') {
                    uri[ulen-1] = '\0';
                    memmove(uri, uri+1, ulen-1);
                }
                size_t plen = strlen(path);
                if (plen >= 2 && path[0] == '"' && path[plen-1] == '"') {
                    path[plen-1] = '\0';
                    memmove(path, path+1, plen-1);
                }
                fprintf(fp, "\ncontext %s {\n", uri);
                fprintf(fp, "  type                    NULL\n");
                fprintf(fp, "  location                %s\n", path);
                fprintf(fp, "  allowBrowse             1\n");
                fprintf(fp, "}\n");
            }
            line = strtok_r(NULL, "\n", &saveptr);
        }
        free(aliases);
    }

    fclose(fp);
    return 0;
}

/* ---- main entry point ---- */

int ols_write_config(const ap_config_t *config, const char *output_dir) {
    if (!config || !output_dir) return -1;

    /* Create output directory */
    if (mkdirs(output_dir) != 0 && errno != EEXIST) {
        fprintf(stderr, "litehttpd-confconv: cannot create %s: %s\n",
                output_dir, strerror(errno));
        return -1;
    }

    /* Write listener config to stdout (for manual integration) */
    char listeners_path[4096];
    snprintf(listeners_path, sizeof(listeners_path),
             "%s/listeners.conf", output_dir);
    FILE *lf = fopen(listeners_path, "w");
    if (!lf) {
        fprintf(stderr, "litehttpd-confconv: cannot create %s: %s\n",
                listeners_path, strerror(errno));
        return -1;
    }

    /* Collect ports from vhosts and Listen directives */
    int ports_cap = AP_MAX_LISTEN;
    int *ports_done = calloc(ports_cap, sizeof(int));
    if (!ports_done) { fclose(lf); unlink(listeners_path); return -1; }
    int ports_count = 0;

    /* From Listen directives */
    for (int i = 0; i < config->listen_count; i++) {
        int port = config->listen_ports[i];
        int ssl = config->listen_ssl[i];
        int mapped = port;
        /* Apply portmap */
        for (int j = 0; j < config->port_map_count; j++) {
            if (config->port_from[j] == port) {
                mapped = config->port_to[j];
                break;
            }
        }
        /* Find SSL cert/key from vhosts on this port */
        const char *cert = NULL, *key = NULL, *chain = NULL;
        if (ssl) {
            for (int v = 0; v < config->vhost_count; v++) {
                if (config->vhosts[v].listen_ssl &&
                    config->vhosts[v].ssl_cert) {
                    cert = config->vhosts[v].ssl_cert;
                    key = config->vhosts[v].ssl_key;
                    chain = config->vhosts[v].ssl_chain;
                    break;
                }
            }
        }
        ols_write_listener(lf, port, ssl, mapped, cert, key, chain);
        if (ports_count >= ports_cap) {
            ports_cap *= 2;
            int *tmp = realloc(ports_done, ports_cap * sizeof(int));
            if (!tmp) { free(ports_done); fclose(lf); unlink(listeners_path); return -1; }
            ports_done = tmp;
        }
        ports_done[ports_count++] = port;
    }

    /* From vhosts (if port not already covered) */
    for (int v = 0; v < config->vhost_count; v++) {
        int port = config->vhosts[v].listen_port;
        if (port <= 0) continue;
        int found = 0;
        for (int j = 0; j < ports_count; j++) {
            if (ports_done[j] == port) { found = 1; break; }
        }
        if (found) continue;
        int ssl = config->vhosts[v].listen_ssl;
        int mapped = port;
        for (int j = 0; j < config->port_map_count; j++) {
            if (config->port_from[j] == port) {
                mapped = config->port_to[j];
                break;
            }
        }
        ols_write_listener(lf, port, ssl, mapped,
                           config->vhosts[v].ssl_cert,
                           config->vhosts[v].ssl_key,
                           config->vhosts[v].ssl_chain);
        if (ports_count >= ports_cap) {
            ports_cap *= 2;
            int *tmp = realloc(ports_done, ports_cap * sizeof(int));
            if (!tmp) { free(ports_done); fclose(lf); unlink(listeners_path); return -1; }
            ports_done = tmp;
        }
        ports_done[ports_count++] = port;
    }

    /* Write listener vhost mappings */
    for (int v = 0; v < config->vhost_count; v++) {
        const ap_vhost_t *vh = &config->vhosts[v];
        const char *name = vh->server_name;
        if (!name) name = config->server_name;
        char listener_namebuf[64];
        if (!name) {
            /* Generate a name */
            snprintf(listener_namebuf, sizeof(listener_namebuf), "vhost%d", v);
            name = listener_namebuf;
        }

        /* Map vhost to listener */
        int port = vh->listen_port > 0 ? vh->listen_port : 80;
        int mapped = port;
        for (int j = 0; j < config->port_map_count; j++) {
            if (config->port_from[j] == port) {
                mapped = config->port_to[j];
                break;
            }
        }
        fprintf(lf, "# listener %s%d map %s\n",
                vh->listen_ssl ? "SSL" : "Default", port, name);
        /* Build domain list */
        if (vh->server_name) {
            fprintf(lf, "# map %s %s", name, vh->server_name);
            if (vh->server_aliases)
                fprintf(lf, ", %s", vh->server_aliases);
            fprintf(lf, "\n");
        }
        fprintf(lf, "\n");
    }

    fclose(lf);
    free(ports_done);

    /* Write virtualhost registration and vhconf.conf files */
    char vhosts_path[4096];
    snprintf(vhosts_path, sizeof(vhosts_path),
             "%s/vhosts.conf", output_dir);
    FILE *vf = fopen(vhosts_path, "w");
    if (!vf) {
        fprintf(stderr, "litehttpd-confconv: cannot create %s: %s\n",
                vhosts_path, strerror(errno));
        return -1;
    }

    int vhosts_written = 0;
    for (int v = 0; v < config->vhost_count; v++) {
        const ap_vhost_t *vh = &config->vhosts[v];
        const char *name = vh->server_name;
        if (!name) name = config->server_name;
        char namebuf[64];
        if (!name) {
            snprintf(namebuf, sizeof(namebuf), "vhost%d", v);
            name = namebuf;
        }

        /* Sanitize name for filesystem use */
        char safe_name[256];
        if (sanitize_server_name(name, safe_name, sizeof(safe_name)) != 0) {
            snprintf(safe_name, sizeof(safe_name), "vhost%d", v);
        }

        /* Create vhost directory */
        char vhost_dir[4096];
        snprintf(vhost_dir, sizeof(vhost_dir),
                 "%s/vhosts/%s", output_dir, safe_name);
        mkdirs(vhost_dir);

        /* Write vhconf.conf */
        char vhconf_path[4096];
        snprintf(vhconf_path, sizeof(vhconf_path),
                 "%s/vhconf.conf", vhost_dir);
        if (ols_write_vhost(vh, vhconf_path) != 0) {
            /* Failed — skip registration to avoid pointing at truncated file */
            unlink(vhconf_path);
            continue;
        }
        vhosts_written++;

        /* Write virtualhost registration */
        const char *doc_root = vh->doc_root ? vh->doc_root : "/var/www/html";

        /* Determine vhRoot: use parent of doc_root if it ends with /html,
         * /public_html, etc., otherwise use doc_root itself */
        char vh_root[4096];
        strncpy(vh_root, doc_root, sizeof(vh_root) - 1);
        vh_root[sizeof(vh_root) - 1] = '\0';

        /* Use original name for config content, safe_name for paths */
        fprintf(vf, "virtualhost %s {\n", name);
        fprintf(vf, "  vhRoot                  %s\n", vh_root);
        fprintf(vf, "  configFile              %s/vhosts/%s/vhconf.conf\n",
                output_dir, safe_name);
        fprintf(vf, "  allowSymbolLink         1\n");
        fprintf(vf, "  enableScript            1\n");
        fprintf(vf, "  restrained              0\n");
        if (vh->server_admin)
            fprintf(vf, "  adminEmails             %s\n", vh->server_admin);
        fprintf(vf, "}\n\n");
    }

    fclose(vf);

    fprintf(stdout, "litehttpd-confconv: wrote %d vhost(s), %d listener(s) to %s\n",
            vhosts_written, ports_count, output_dir);

    return 0;
}
