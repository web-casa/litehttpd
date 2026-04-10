/*
 * apacheconf_parser.c -- Apache httpd.conf parser
 *
 * Reads Apache config files and populates ap_config_t structure.
 * Supports Include/IncludeOptional with glob, <VirtualHost>, <Directory>,
 * <Location>, <IfModule> (transparent), and 40 common directives.
 */
#include "apacheconf_parser.h"

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- helpers ---- */

static char *safe_strdup(const char *s) {
    return s ? strdup(s) : NULL;
}

static void safe_free(char **p) {
    if (p && *p) { free(*p); *p = NULL; }
}

/* Append text to a dynamic string field (realloc + newline-separated). */
static void str_append(char **dest, const char *text) {
    if (!text || !text[0]) return;
    if (!*dest) {
        *dest = strdup(text);
        return;
    }
    size_t old_len = strlen(*dest);
    size_t add_len = strlen(text);
    char *buf = realloc(*dest, old_len + 1 + add_len + 1);
    if (!buf) return;
    buf[old_len] = '\n';
    memcpy(buf + old_len + 1, text, add_len + 1);
    *dest = buf;
}

/* Trim leading/trailing whitespace in-place, return pointer into buf. */
static char *trim(char *buf) {
    while (*buf && isspace((unsigned char)*buf)) buf++;
    if (!*buf) return buf;
    char *end = buf + strlen(buf) - 1;
    while (end > buf && isspace((unsigned char)*end)) *end-- = '\0';
    return buf;
}

/* Case-insensitive string comparison. */
static int streqi(const char *a, const char *b) {
    return strcasecmp(a, b) == 0;
}

/* Extract the first whitespace-delimited word from *p, advance *p past it. */
static char *next_word(char **p) {
    char *s = *p;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) { *p = s; return NULL; }
    char *start = s;
    while (*s && !isspace((unsigned char)*s)) s++;
    if (*s) *s++ = '\0';
    *p = s;
    return start;
}

/* Get the rest of the line after the current position, trimmed. */
static char *rest_of_line(char *p) {
    return trim(p);
}

/* ---- Expires duration parser ---- */

long ap_parse_expires_duration(const char *duration) {
    if (!duration || !duration[0]) return 0;

    /* Skip leading quote */
    const char *p = duration;
    if (*p == '"') p++;

    /* Skip "access" or "modification" prefix */
    if (strncasecmp(p, "access", 6) == 0) p += 6;
    else if (strncasecmp(p, "modification", 12) == 0) p += 12;
    else if (*p == 'A' || *p == 'M') p++;

    /* Skip whitespace and "plus" */
    while (*p && isspace((unsigned char)*p)) p++;
    if (strncasecmp(p, "plus", 4) == 0) { p += 4; }
    while (*p && isspace((unsigned char)*p)) p++;

    long total_seconds = 0;
    while (*p && *p != '"') {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p || *p == '"') break;

        char *endptr = NULL;
        long num = strtol(p, &endptr, 10);
        if (endptr == p) break;
        p = endptr;

        while (*p && isspace((unsigned char)*p)) p++;

        /* Parse unit */
        if (strncasecmp(p, "year", 4) == 0) {
            if (num > LONG_MAX / 31536000) return 0; /* overflow protection */
            total_seconds += num * 365 * 86400;
            p += 4;
        } else if (strncasecmp(p, "month", 5) == 0) {
            if (num > LONG_MAX / 2592000) return 0; /* overflow protection */
            total_seconds += num * 30 * 86400;
            p += 5;
        } else if (strncasecmp(p, "week", 4) == 0) {
            if (num > LONG_MAX / 604800) return 0; /* overflow protection */
            total_seconds += num * 7 * 86400;
            p += 4;
        } else if (strncasecmp(p, "day", 3) == 0) {
            if (num > LONG_MAX / 86400) return 0; /* overflow protection */
            total_seconds += num * 86400;
            p += 3;
        } else if (strncasecmp(p, "hour", 4) == 0) {
            total_seconds += num * 3600;
            p += 4;
        } else if (strncasecmp(p, "minute", 6) == 0) {
            total_seconds += num * 60;
            p += 6;
        } else if (strncasecmp(p, "second", 6) == 0) {
            total_seconds += num;
            p += 6;
        } else {
            /* No unit, treat as seconds */
            total_seconds += num;
            break;
        }
        /* Skip plural 's' */
        if (*p == 's') p++;
    }

    return total_seconds;
}

/* ---- Panel detection ---- */

panel_type_t ap_detect_panel(const char *config_path) {
    struct stat st;
    if (stat("/usr/local/directadmin", &st) == 0 &&
        stat("/etc/httpd/conf/extra/directadmin-vhosts.conf", &st) == 0 &&
        (!config_path || strncmp(config_path, "/etc/httpd/conf/", 16) == 0))
        return CP_DIRECTADMIN;
    if (stat("/usr/local/interworx", &st) == 0 &&
        stat("/etc/httpd/conf.d/iworx.conf", &st) == 0 &&
        (!config_path || strncmp(config_path, "/etc/httpd/conf/", 16) == 0))
        return CP_INTERWORX;
    if (stat("/usr/local/CyberCP", &st) == 0 &&
        (!config_path ||
         strncmp(config_path, "/usr/local/apache/conf/", 23) == 0 ||
         strncmp(config_path, "/etc/apache2/conf/", 18) == 0))
        return CP_CYBERCP;
    return CP_NONE;
}

/* ---- Hot reload state check ---- */

/* Internal: compare current stat against saved state.
 * Returns: 1 if changed, 0 if unchanged, -1 on error.
 */
static int check_changed_internal(const char *config_path,
                                  const char *state_file) {
    if (!config_path || !state_file) return -1;

    struct stat st;
    if (stat(config_path, &st) != 0) return -1;

    unsigned long cur_ino = (unsigned long)st.st_ino;
    long cur_ctime_nsec = (long)st.st_ctim.tv_nsec;
    long cur_blocks = (long)st.st_blocks;
    long cur_size = (long)st.st_size;

    /* Read saved state */
    FILE *sf = fopen(state_file, "r");
    if (sf) {
        unsigned long saved_ino = 0;
        long saved_ctime_nsec = 0, saved_blocks = 0, saved_size = 0;
        if (fscanf(sf, "%lu %ld %ld %ld", &saved_ino, &saved_ctime_nsec,
                   &saved_blocks, &saved_size) == 4) {
            fclose(sf);
            if (saved_ino == cur_ino && saved_ctime_nsec == cur_ctime_nsec &&
                saved_blocks == cur_blocks && saved_size == cur_size) {
                return 0; /* unchanged */
            }
        } else {
            fclose(sf);
        }
    }

    return 1; /* changed */
}

int ap_save_config_state(const char *config_path, const char *state_file) {
    if (!config_path || !state_file) return -1;

    struct stat st;
    if (stat(config_path, &st) != 0) return -1;

    unsigned long cur_ino = (unsigned long)st.st_ino;
    long cur_ctime_nsec = (long)st.st_ctim.tv_nsec;
    long cur_blocks = (long)st.st_blocks;
    long cur_size = (long)st.st_size;

    /* Atomic write: write to temp file, then rename */
    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", state_file);
    FILE *sf = fopen(tmp_path, "w");
    if (!sf) return -1;
    fprintf(sf, "%lu %ld %ld %ld\n", cur_ino, cur_ctime_nsec,
            cur_blocks, cur_size);
    fclose(sf);
    if (rename(tmp_path, state_file) != 0) {
        unlink(tmp_path);
        return -1;
    }
    return 0;
}

int ap_check_config_changed_no_save(const char *config_path,
                                    const char *state_file) {
    return check_changed_internal(config_path, state_file);
}

int ap_check_config_changed(const char *config_path, const char *state_file) {
    int rc = check_changed_internal(config_path, state_file);
    if (rc == 1) {
        /* Save new state */
        ap_save_config_state(config_path, state_file);
    }
    return rc;
}

/* ---- forward declarations ---- */

static int ap_parse_stream(FILE *fp, ap_config_t *config,
                           ap_vhost_t *current_vhost, int depth);
static int ap_process_include(const char *pattern, int is_optional,
                              ap_config_t *config,
                              ap_vhost_t *current_vhost, int depth);
static int ap_parse_line(char *line, ap_config_t *config,
                         ap_vhost_t *current_vhost, FILE *fp, int depth);
static int ap_parse_vhost_block(FILE *fp, const char *addr,
                                ap_config_t *config, int depth);
static int ap_parse_directory_block(FILE *fp, const char *type,
                                    const char *path, ap_vhost_t *vhost,
                                    int depth);

/* ---- context helpers ---- */

static void ap_context_init(ap_context_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->allow_browse = -1;
    ctx->follow_symlinks = -1;
    ctx->require_all = -1;
    ctx->order = 0;
}

static void ap_context_free(ap_context_t *ctx) {
    safe_free(&ctx->type);
    safe_free(&ctx->path);
    safe_free(&ctx->options);
    safe_free(&ctx->allow_override);
    safe_free(&ctx->dir_index);
    safe_free(&ctx->handler);
    safe_free(&ctx->access_allow);
    safe_free(&ctx->access_deny);
}

/* ---- vhost helpers ---- */

static void ap_vhost_init(ap_vhost_t *vh) {
    memset(vh, 0, sizeof(*vh));
    vh->ssl_session_tickets = -1;
    vh->ssl_verify_depth = 1;
}

static void ap_vhost_free(ap_vhost_t *vh) {
    safe_free(&vh->server_name);
    safe_free(&vh->server_aliases);
    safe_free(&vh->server_admin);
    safe_free(&vh->doc_root);
    safe_free(&vh->ssl_cert);
    safe_free(&vh->ssl_key);
    safe_free(&vh->ssl_chain);
    safe_free(&vh->php_handler);
    safe_free(&vh->rewrite_rules);
    safe_free(&vh->extra_headers);
    safe_free(&vh->error_pages);
    safe_free(&vh->redirect_rules);
    safe_free(&vh->allow_override);
    safe_free(&vh->dir_index);
    safe_free(&vh->options);
    safe_free(&vh->aliases);
    safe_free(&vh->php_values);
    safe_free(&vh->php_flags);
    safe_free(&vh->php_admin_values);
    safe_free(&vh->php_admin_flags);
    safe_free(&vh->ssl_protocol);
    safe_free(&vh->ssl_cipher_suite);
    safe_free(&vh->proxy_pass);
    safe_free(&vh->proxy_pass_reverse);
    safe_free(&vh->expires_by_type);
    safe_free(&vh->suexec_user);
    safe_free(&vh->suexec_group);
    safe_free(&vh->env_vars);
    safe_free(&vh->ssl_ca_cert_file);
    safe_free(&vh->ssl_ca_cert_path);
    safe_free(&vh->ssl_ca_rev_file);
    safe_free(&vh->ssl_ocsp_responder);
    for (int i = 0; i < vh->context_count; i++)
        ap_context_free(&vh->contexts[i]);
}

/* ---- parse Listen directive ---- */

static void ap_parse_listen(const char *value, ap_config_t *config) {
    if (config->listen_count >= AP_MAX_LISTEN) return;
    /* Format: [addr:]port or [addr:]port https */
    long port = 0;
    int ssl = 0;
    /* Try to extract port number */
    const char *colon = strrchr(value, ':');
    const char *port_str = colon ? colon + 1 : value;
    char *endptr = NULL;
    port = strtol(port_str, &endptr, 10);
    /* Validate: must consume at least one digit, remaining must be space/end */
    if (endptr == port_str || (*endptr && !isspace((unsigned char)*endptr)))
        return;
    if (port <= 0 || port > 65535) return;
    /* Check for "https" suffix */
    if (strcasestr(value, "https")) ssl = 1;
    if (port == 443) ssl = 1;
    /* Avoid duplicates */
    for (int i = 0; i < config->listen_count; i++) {
        if (config->listen_ports[i] == (int)port) return;
    }
    config->listen_ports[config->listen_count] = (int)port;
    config->listen_ssl[config->listen_count] = ssl;
    config->listen_count++;
}

/* ---- parse portmap option ---- */

static void ap_parse_portmap(const char *spec, ap_config_t *config) {
    /* portmap=80:8088,443:8443 */
    char buf[256];
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *saveptr = NULL;
    char *tok = strtok_r(buf, ",", &saveptr);
    while (tok && config->port_map_count < AP_MAX_PORTMAP) {
        int from = 0, to = 0;
        if (sscanf(tok, "%d:%d", &from, &to) == 2 && from > 0 && to > 0) {
            config->port_from[config->port_map_count] = from;
            config->port_to[config->port_map_count] = to;
            config->port_map_count++;
        }
        tok = strtok_r(NULL, ",", &saveptr);
    }
}

/* Map a port through portmap. */
static int ap_map_port(const ap_config_t *config, int port) {
    for (int i = 0; i < config->port_map_count; i++) {
        if (config->port_from[i] == port)
            return config->port_to[i];
    }
    return port;
}

/* ---- parse Options directive ---- */

static void ap_parse_options(const char *value, ap_context_t *ctx) {
    if (!ctx) return;
    safe_free(&ctx->options);
    ctx->options = strdup(value);
    /* Parse +/- Indexes and FollowSymLinks */
    char buf[AP_MAX_LINE];
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *saveptr = NULL;
    char *tok = strtok_r(buf, " \t", &saveptr);
    while (tok) {
        if (streqi(tok, "Indexes") || streqi(tok, "+Indexes"))
            ctx->allow_browse = 1;
        else if (streqi(tok, "-Indexes"))
            ctx->allow_browse = 0;
        else if (streqi(tok, "FollowSymLinks") || streqi(tok, "+FollowSymLinks"))
            ctx->follow_symlinks = 1;
        else if (streqi(tok, "-FollowSymLinks"))
            ctx->follow_symlinks = 0;
        else if (streqi(tok, "None")) {
            ctx->allow_browse = 0;
            ctx->follow_symlinks = 0;
        } else if (streqi(tok, "All")) {
            ctx->allow_browse = 1;
            ctx->follow_symlinks = 1;
        }
        tok = strtok_r(NULL, " \t", &saveptr);
    }
}

/* ---- parse Options at vhost level ---- */

static void ap_parse_options_vhost(const char *value, ap_vhost_t *vh) {
    safe_free(&vh->options);
    vh->options = strdup(value);
}

/* ---- detect PHP from handler string ---- */

static void ap_detect_php(const char *handler, ap_vhost_t *vh) {
    if (!handler) return;
    /* Check for PHP-related handler patterns */
    if (!strcasestr(handler, "php") &&
        !strcasestr(handler, "x-httpd-php") &&
        !strcasestr(handler, "fcgi") &&
        !strcasestr(handler, "proxy:unix:") &&
        !strcasestr(handler, "proxy:fcgi:"))
        return;

    /* Try to extract version number from patterns like php8.3, php83, lsphp83 */
    int major = 0, minor = 0;
    const char *p = handler;
    while (*p) {
        /* Look for "php" prefix (case-insensitive) */
        if ((p[0] == 'p' || p[0] == 'P') &&
            (p[1] == 'h' || p[1] == 'H') &&
            (p[2] == 'p' || p[2] == 'P')) {
            const char *v = p + 3;
            if (*v == '-') v++;  /* skip optional dash */
            if (*v >= '5' && *v <= '9') {
                major = *v - '0'; v++;
                if (*v == '.') v++;
                if (*v >= '0' && *v <= '9') {
                    minor = *v - '0';
                }
                break;
            }
        }
        p++;
    }

    safe_free(&vh->php_handler);
    if (major > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "lsphp%d%d", major, minor);
        vh->php_handler = strdup(buf);
    } else {
        vh->php_handler = strdup(handler);
    }
}

/* ---- parse <VirtualHost addr> block ---- */

static int ap_parse_vhost_block(FILE *fp, const char *addr,
                                ap_config_t *config, int depth) {
    if (config->vhost_count >= AP_MAX_VHOSTS) {
        fprintf(stderr, "litehttpd-confconv: too many VirtualHosts (max %d)\n",
                AP_MAX_VHOSTS);
        return -1;
    }
    ap_vhost_t *vh = &config->vhosts[config->vhost_count];
    ap_vhost_init(vh);

    /* Extract port from address like *:80 or 1.2.3.4:443 */
    const char *colon = strrchr(addr, ':');
    if (colon) {
        char *endptr = NULL;
        long port = strtol(colon + 1, &endptr, 10);
        if (endptr != colon + 1 && port > 0 && port <= 65535) {
            vh->listen_port = (int)port;
            vh->listen_ssl = (port == 443) ? 1 : 0;
        }
    }

    char line[AP_MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = trim(line);
        if (!trimmed[0] || trimmed[0] == '#') continue;

        /* End of VirtualHost block */
        if (trimmed[0] == '<' && trimmed[1] == '/') {
            /* Validate close tag matches VirtualHost */
            char close_tag[64] = {0};
            sscanf(trimmed + 2, "%63[^> \t]", close_tag);
            if (strcasecmp(close_tag, "VirtualHost") != 0) {
                fprintf(stderr, "litehttpd-confconv: warning: expected </VirtualHost>, "
                        "got </%s>\n", close_tag);
                continue; /* skip mismatched close tag */
            }
            config->vhost_count++;
            return 0;
        }

        /* Nested <Directory> or <Location> blocks */
        if (trimmed[0] == '<' && trimmed[1] != '/') {
            char tag[64] = {0};
            char path[AP_MAX_LINE] = {0};
            if (sscanf(trimmed, "<%63s %4095[^>]>", tag, path) >= 1) {
                /* Remove trailing > from path if present */
                char *gt = strchr(path, '>');
                if (gt) *gt = '\0';
                char *p = trim(path);
                /* Strip surrounding quotes */
                size_t plen = strlen(p);
                if (plen >= 2 && p[0] == '"' && p[plen-1] == '"') {
                    p[plen-1] = '\0';
                    p++;
                }

                if (strcasecmp(tag, "Directory") == 0 ||
                    strcasecmp(tag, "Location") == 0 ||
                    strcasecmp(tag, "LocationMatch") == 0) {
                    ap_parse_directory_block(fp, tag, p, vh, depth);
                    continue;
                }
                if (strcasecmp(tag, "IfModule") == 0 ||
                    strcasecmp(tag, "IfDefine") == 0) {
                    /* Transparent: continue parsing contents */
                    continue;
                }
                if (strcasecmp(tag, "Files") == 0 ||
                    strcasecmp(tag, "FilesMatch") == 0) {
                    /* Skip Files blocks for now */
                    while (fgets(line, sizeof(line), fp)) {
                        trimmed = trim(line);
                        if (trimmed[0] == '<' && trimmed[1] == '/' &&
                            (strcasestr(trimmed, "Files")))
                            break;
                    }
                    continue;
                }
            }
            continue;
        }

        /* Close of IfModule/IfDefine */
        if (trimmed[0] == '<' && trimmed[1] == '/') continue;

        /* Parse directive within vhost */
        ap_parse_line(trimmed, config, vh, fp, depth);
    }

    /* Unterminated block -- still count it */
    config->vhost_count++;
    return 0;
}

/* ---- parse <Directory>/<Location> block ---- */

static int ap_parse_directory_block(FILE *fp, const char *type,
                                    const char *path, ap_vhost_t *vhost,
                                    int depth) {
    if (!vhost || vhost->context_count >= AP_MAX_CONTEXTS) return -1;
    ap_context_t *ctx = &vhost->contexts[vhost->context_count];
    ap_context_init(ctx);
    ctx->type = strdup(type);
    ctx->path = strdup(path);

    char line[AP_MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = trim(line);
        if (!trimmed[0] || trimmed[0] == '#') continue;

        /* End of block */
        if (trimmed[0] == '<' && trimmed[1] == '/') {
            /* Validate close tag matches the opening block type */
            char close_tag[64] = {0};
            sscanf(trimmed + 2, "%63[^> \t]", close_tag);
            if (strcasecmp(close_tag, type) == 0) {
                vhost->context_count++;
                return 0;
            }
            /* Also accept IfModule/IfDefine close tags (transparent blocks) */
            if (strcasecmp(close_tag, "IfModule") == 0 ||
                strcasecmp(close_tag, "IfDefine") == 0) {
                continue;
            }
            fprintf(stderr, "litehttpd-confconv: warning: expected </%s>, got </%s>\n",
                    type, close_tag);
            continue;
        }

        /* Nested IfModule: transparent */
        if (trimmed[0] == '<') {
            char tag[64] = {0};
            sscanf(trimmed, "<%63s", tag);
            char *gt = strchr(tag, '>');
            if (gt) *gt = '\0';
            if (strcasecmp(tag, "IfModule") == 0 ||
                strcasecmp(tag, "IfDefine") == 0) {
                continue;
            }
            if (trimmed[1] == '/') continue;
            /* Skip unknown nested blocks */
            while (fgets(line, sizeof(line), fp)) {
                char *t = trim(line);
                if (t[0] == '<' && t[1] == '/') break;
            }
            continue;
        }

        /* Parse directives inside context */
        char *p = trimmed;
        char *directive = next_word(&p);
        if (!directive) continue;
        char *value = rest_of_line(p);

        if (streqi(directive, "Options")) {
            ap_parse_options(value, ctx);
        } else if (streqi(directive, "AllowOverride")) {
            safe_free(&ctx->allow_override);
            ctx->allow_override = strdup(value);
        } else if (streqi(directive, "DirectoryIndex")) {
            safe_free(&ctx->dir_index);
            ctx->dir_index = strdup(value);
        } else if (streqi(directive, "SetHandler")) {
            safe_free(&ctx->handler);
            ctx->handler = strdup(value);
            ap_detect_php(value, vhost);
        } else if (streqi(directive, "AddHandler")) {
            ap_detect_php(value, vhost);
        } else if (streqi(directive, "FcgidWrapper")) {
            ap_detect_php(value, vhost);
        } else if (streqi(directive, "Allow")) {
            /* "Allow from ..." */
            if (strncasecmp(value, "from ", 5) == 0) {
                safe_free(&ctx->access_allow);
                ctx->access_allow = strdup(value + 5);
            }
        } else if (streqi(directive, "Deny")) {
            if (strncasecmp(value, "from ", 5) == 0) {
                safe_free(&ctx->access_deny);
                ctx->access_deny = strdup(value + 5);
            }
        } else if (streqi(directive, "Order")) {
            if (strcasestr(value, "deny,allow"))
                ctx->order = 1;
            else
                ctx->order = 0;
        } else if (streqi(directive, "Require")) {
            if (strcasestr(value, "all granted"))
                ctx->require_all = 1;
            else if (strcasestr(value, "all denied"))
                ctx->require_all = 0;
        } else if (streqi(directive, "RewriteEngine")) {
            if (strcasestr(value, "on"))
                vhost->rewrite_enabled = 1;
        } else if (streqi(directive, "RewriteCond") ||
                   streqi(directive, "RewriteRule")) {
            char buf[AP_MAX_LINE];
            snprintf(buf, sizeof(buf), "%s %s", directive, value);
            str_append(&vhost->rewrite_rules, buf);
        } else if (streqi(directive, "RewriteBase")) {
            char buf[AP_MAX_LINE];
            snprintf(buf, sizeof(buf), "RewriteBase %s", value);
            str_append(&vhost->rewrite_rules, buf);
        } else if (streqi(directive, "Header") ||
                   streqi(directive, "RequestHeader")) {
            char buf[AP_MAX_LINE];
            snprintf(buf, sizeof(buf), "%s %s", directive, value);
            str_append(&vhost->extra_headers, buf);
        } else if (streqi(directive, "ErrorDocument")) {
            str_append(&vhost->error_pages, value);
        }
    }

    vhost->context_count++;
    return 0;
}

/* ---- Include/IncludeOptional with glob ---- */

static int ap_process_include(const char *pattern, int is_optional,
                              ap_config_t *config,
                              ap_vhost_t *current_vhost, int depth) {
    if (depth >= AP_MAX_INCLUDE_DEPTH) {
        fprintf(stderr, "litehttpd-confconv: include depth limit reached (%d)\n",
                AP_MAX_INCLUDE_DEPTH);
        return -1;
    }

    glob_t g;
    int flags = GLOB_NOSORT;
    if (is_optional) flags |= GLOB_NOCHECK;
    int rc = glob(pattern, flags, NULL, &g);
    if (rc != 0) {
        globfree(&g);
        if (is_optional) return 0;
        fprintf(stderr, "litehttpd-confconv: Include glob failed for %s\n", pattern);
        return -1;
    }

    /* For non-optional Include, verify we got real matches */
    if (!is_optional && g.gl_pathc == 0) {
        globfree(&g);
        fprintf(stderr, "litehttpd-confconv: Include matched no files: %s\n", pattern);
        return -1;
    }

    for (size_t i = 0; i < g.gl_pathc; i++) {
        /* Path traversal check: reject paths containing ".." */
        if (strstr(g.gl_pathv[i], "..")) {
            fprintf(stderr, "litehttpd-confconv: rejecting path with '..': %s\n",
                    g.gl_pathv[i]);
            continue;
        }

        /* Symlink / escape check: canonicalize and verify under server_root
         * or under the directory of the include pattern itself. */
        char resolved[PATH_MAX];
        if (realpath(g.gl_pathv[i], resolved) == NULL) {
            if (!is_optional)
                fprintf(stderr, "litehttpd-confconv: cannot resolve: %s\n",
                        g.gl_pathv[i]);
            continue;
        }
        if (config->server_root) {
            /* Also allow files under the pattern's parent directory */
            char pat_dir[PATH_MAX];
            strncpy(pat_dir, pattern, sizeof(pat_dir) - 1);
            pat_dir[sizeof(pat_dir) - 1] = '\0';
            char *slash = strrchr(pat_dir, '/');
            if (slash) *slash = '\0';
            char resolved_pat_dir[PATH_MAX];
            int under_server_root = (strncmp(resolved, config->server_root,
                                             strlen(config->server_root)) == 0);
            int under_pat_dir = 0;
            if (realpath(pat_dir, resolved_pat_dir) != NULL) {
                under_pat_dir = (strncmp(resolved, resolved_pat_dir,
                                         strlen(resolved_pat_dir)) == 0);
            }
            if (!under_server_root && !under_pat_dir) {
                fprintf(stderr,
                        "litehttpd-confconv: Include path outside server root: %s\n",
                        resolved);
                continue;
            }
        }

        struct stat st;
        if (stat(g.gl_pathv[i], &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;
        if (st.st_size > AP_MAX_FILE_SIZE) {
            fprintf(stderr, "litehttpd-confconv: file too large: %s (%ld bytes)\n",
                    g.gl_pathv[i], (long)st.st_size);
            continue;
        }

        FILE *fp = fopen(g.gl_pathv[i], "r");
        if (!fp) continue;
        ap_parse_stream(fp, config, current_vhost, depth + 1);
        fclose(fp);
    }

    globfree(&g);
    return 0;
}

/* Recursive macro expansion depth guard */
static int s_macro_depth = 0;

/* ---- main line dispatcher ---- */

static int ap_parse_line(char *line, ap_config_t *config,
                         ap_vhost_t *current_vhost, FILE *fp, int depth) {
    /* ---- Block openers: parse before next_word modifies the buffer ---- */
    if (line[0] == '<' && line[1] != '/') {
        char tag[64] = {0};
        char args[AP_MAX_LINE] = {0};
        if (sscanf(line, "<%63s %4095[^>]>", tag, args) >= 1) {
            char *gt = strchr(tag, '>');
            if (gt) *gt = '\0';
            char *a = trim(args);

            if (strcasecmp(tag, "VirtualHost") == 0) {
                return ap_parse_vhost_block(fp, a, config, depth);
            }
            if (strcasecmp(tag, "Directory") == 0 ||
                strcasecmp(tag, "Location") == 0 ||
                strcasecmp(tag, "LocationMatch") == 0) {
                /* Strip quotes */
                size_t alen = strlen(a);
                if (alen >= 2 && a[0] == '"' && a[alen-1] == '"') {
                    a[alen-1] = '\0'; a++;
                }
                if (current_vhost) {
                    return ap_parse_directory_block(fp, tag, a,
                                                   current_vhost, depth);
                }
                /* Global context: create a default vhost if needed */
                /* Skip for now */
                while (fgets(line, AP_MAX_LINE, fp)) {
                    char *t = trim(line);
                    if (t[0] == '<' && t[1] == '/') break;
                }
                return 0;
            }
            if (strcasecmp(tag, "IfModule") == 0 ||
                strcasecmp(tag, "IfDefine") == 0) {
                /* Transparent: just continue */
                return 0;
            }
            if (strcasecmp(tag, "Macro") == 0) {
                /* <Macro name $p1 $p2 ...> */
                if (config->macro_count >= AP_MAX_MACROS) {
                    fprintf(stderr, "litehttpd-confconv: too many macros (max %d)\n",
                            AP_MAX_MACROS);
                    /* skip to </Macro> */
                    while (fgets(line, AP_MAX_LINE, fp)) {
                        char *t = trim(line);
                        if (t[0] == '<' && t[1] == '/' &&
                            strcasestr(t, "Macro")) break;
                    }
                    return 0;
                }
                ap_macro_t *macro = &config->macros[config->macro_count];
                memset(macro, 0, sizeof(*macro));
                /* Parse name and params from args */
                char *ap = a;
                char *mname = next_word(&ap);
                if (mname)
                    macro->name = strdup(mname);
                /* Parse params ($xxx or @xxx) */
                char *param;
                while ((param = next_word(&ap)) != NULL &&
                       macro->param_count < AP_MAX_MACRO_PARAMS) {
                    if (param[0] == '$' || param[0] == '@')
                        macro->params[macro->param_count++] = strdup(param);
                }
                /* Read body until </Macro> */
                macro->body = malloc(AP_MAX_MACRO_BODY);
                if (!macro->body) return -1;
                macro->body[0] = '\0';
                macro->body_len = 0;
                char mline[AP_MAX_LINE];
                while (fgets(mline, sizeof(mline), fp)) {
                    char *mt = trim(mline);
                    if (mt[0] == '<' && mt[1] == '/' &&
                        strcasestr(mt, "Macro")) break;
                    int len = (int)strlen(mt);
                    if (macro->body_len + len + 2 < AP_MAX_MACRO_BODY) {
                        if (macro->body_len > 0)
                            macro->body[macro->body_len++] = '\n';
                        memcpy(macro->body + macro->body_len, mt, len);
                        macro->body_len += len;
                        macro->body[macro->body_len] = '\0';
                    }
                }
                config->macro_count++;
                return 0;
            }
            /* Unknown block: skip to close */
            while (fgets(line, AP_MAX_LINE, fp)) {
                char *t = trim(line);
                if (t[0] == '<' && t[1] == '/') break;
            }
            return 0;
        }
        return 0;
    }

    /* Close of transparent blocks */
    if (line[0] == '<' && line[1] == '/') return 0;

    /* Parse directive and value */
    char *p = line;
    char *directive = next_word(&p);
    if (!directive) return 0;
    char *value = rest_of_line(p);

    /* ---- Global directives ---- */

    if (streqi(directive, "ServerName")) {
        if (current_vhost) {
            safe_free(&current_vhost->server_name);
            current_vhost->server_name = strdup(value);
        } else {
            safe_free(&config->server_name);
            config->server_name = strdup(value);
        }
        return 0;
    }

    if (streqi(directive, "ServerAlias")) {
        if (current_vhost) {
            str_append(&current_vhost->server_aliases, value);
        }
        return 0;
    }

    if (streqi(directive, "ServerAdmin")) {
        if (current_vhost) {
            safe_free(&current_vhost->server_admin);
            current_vhost->server_admin = strdup(value);
        } else {
            safe_free(&config->server_admin);
            config->server_admin = strdup(value);
        }
        return 0;
    }

    if (streqi(directive, "DocumentRoot")) {
        /* Strip quotes */
        size_t vlen = strlen(value);
        if (vlen >= 2 && value[0] == '"' && value[vlen-1] == '"') {
            value[vlen-1] = '\0'; value++;
        }
        if (current_vhost) {
            safe_free(&current_vhost->doc_root);
            current_vhost->doc_root = strdup(value);
        } else {
            safe_free(&config->doc_root);
            config->doc_root = strdup(value);
        }
        return 0;
    }

    if (streqi(directive, "Listen")) {
        ap_parse_listen(value, config);
        return 0;
    }

    if (streqi(directive, "ServerRoot")) {
        /* Strip quotes */
        size_t vlen = strlen(value);
        if (vlen >= 2 && value[0] == '"' && value[vlen-1] == '"') {
            value[vlen-1] = '\0'; value++;
        }
        safe_free(&config->server_root);
        config->server_root = strdup(value);
        return 0;
    }

    if (streqi(directive, "User")) {
        safe_free(&config->user);
        config->user = strdup(value);
        return 0;
    }

    if (streqi(directive, "Group")) {
        safe_free(&config->group);
        config->group = strdup(value);
        return 0;
    }

    /* ---- SSL ---- */

    if (streqi(directive, "SSLEngine")) {
        if (current_vhost) {
            current_vhost->listen_ssl = strcasestr(value, "on") ? 1 : 0;
        }
        return 0;
    }

    if (streqi(directive, "SSLCertificateFile")) {
        if (current_vhost) {
            safe_free(&current_vhost->ssl_cert);
            current_vhost->ssl_cert = strdup(value);
        }
        return 0;
    }

    if (streqi(directive, "SSLCertificateKeyFile")) {
        if (current_vhost) {
            safe_free(&current_vhost->ssl_key);
            current_vhost->ssl_key = strdup(value);
        }
        return 0;
    }

    if (streqi(directive, "SSLCertificateChainFile")) {
        if (current_vhost) {
            safe_free(&current_vhost->ssl_chain);
            current_vhost->ssl_chain = strdup(value);
        }
        return 0;
    }

    /* ---- Rewrite ---- */

    if (streqi(directive, "RewriteEngine")) {
        if (current_vhost) {
            current_vhost->rewrite_enabled = strcasestr(value, "on") ? 1 : 0;
        }
        return 0;
    }

    if (streqi(directive, "RewriteCond") || streqi(directive, "RewriteRule")) {
        if (current_vhost) {
            char buf[AP_MAX_LINE];
            snprintf(buf, sizeof(buf), "%s %s", directive, value);
            str_append(&current_vhost->rewrite_rules, buf);
        }
        return 0;
    }

    if (streqi(directive, "RewriteBase")) {
        if (current_vhost) {
            char buf[AP_MAX_LINE];
            snprintf(buf, sizeof(buf), "RewriteBase %s", value);
            str_append(&current_vhost->rewrite_rules, buf);
        }
        return 0;
    }

    /* ---- DirectoryIndex ---- */

    if (streqi(directive, "DirectoryIndex")) {
        if (current_vhost) {
            safe_free(&current_vhost->dir_index);
            current_vhost->dir_index = strdup(value);
        }
        return 0;
    }

    /* ---- Options ---- */

    if (streqi(directive, "Options")) {
        if (current_vhost) {
            ap_parse_options_vhost(value, current_vhost);
        }
        return 0;
    }

    /* ---- AllowOverride ---- */

    if (streqi(directive, "AllowOverride")) {
        if (current_vhost) {
            safe_free(&current_vhost->allow_override);
            current_vhost->allow_override = strdup(value);
        }
        return 0;
    }

    /* ---- PHP handlers ---- */

    if (streqi(directive, "SetHandler") ||
        streqi(directive, "AddHandler") ||
        streqi(directive, "FcgidWrapper")) {
        if (current_vhost)
            ap_detect_php(value, current_vhost);
        return 0;
    }

    /* ---- Headers ---- */

    if (streqi(directive, "Header") || streqi(directive, "RequestHeader")) {
        if (current_vhost) {
            char buf[AP_MAX_LINE];
            snprintf(buf, sizeof(buf), "%s %s", directive, value);
            str_append(&current_vhost->extra_headers, buf);
        }
        return 0;
    }

    /* ---- ErrorDocument ---- */

    if (streqi(directive, "ErrorDocument")) {
        if (current_vhost) {
            str_append(&current_vhost->error_pages, value);
        }
        return 0;
    }

    /* ---- Redirect / RedirectMatch ---- */

    if (streqi(directive, "Redirect") || streqi(directive, "RedirectMatch")) {
        if (current_vhost) {
            char buf[AP_MAX_LINE];
            snprintf(buf, sizeof(buf), "%s %s", directive, value);
            str_append(&current_vhost->redirect_rules, buf);
        }
        return 0;
    }

    /* ---- SetEnv ---- */

    if (streqi(directive, "SetEnv")) {
        if (current_vhost) {
            /* Store as "name value" pair */
            str_append(&current_vhost->env_vars, value);
        }
        return 0;
    }

    if (streqi(directive, "SetEnvIf")) {
        /* No-op: OLS has no native SetEnvIf support */
        return 0;
    }

    /* ---- PHP INI overrides ---- */

    if (streqi(directive, "php_value")) {
        if (current_vhost)
            str_append(&current_vhost->php_values, value);
        return 0;
    }

    if (streqi(directive, "php_flag")) {
        if (current_vhost)
            str_append(&current_vhost->php_flags, value);
        return 0;
    }

    if (streqi(directive, "php_admin_value")) {
        if (current_vhost)
            str_append(&current_vhost->php_admin_values, value);
        return 0;
    }

    if (streqi(directive, "php_admin_flag")) {
        if (current_vhost)
            str_append(&current_vhost->php_admin_flags, value);
        return 0;
    }

    /* ---- SSL extended ---- */

    if (streqi(directive, "SSLProtocol")) {
        if (current_vhost) {
            safe_free(&current_vhost->ssl_protocol);
            current_vhost->ssl_protocol = strdup(value);
        }
        return 0;
    }

    if (streqi(directive, "SSLCipherSuite")) {
        if (current_vhost) {
            safe_free(&current_vhost->ssl_cipher_suite);
            current_vhost->ssl_cipher_suite = strdup(value);
        }
        return 0;
    }

    if (streqi(directive, "SSLUseStapling")) {
        if (current_vhost)
            current_vhost->ssl_stapling = strcasestr(value, "on") ? 1 : 0;
        return 0;
    }

    if (streqi(directive, "SSLStaplingCache")) {
        /* Implicitly enable stapling when cache is configured */
        if (current_vhost && !current_vhost->ssl_stapling)
            current_vhost->ssl_stapling = 1;
        return 0;
    }

    if (streqi(directive, "SSLCACertificateFile")) {
        if (current_vhost) {
            safe_free(&current_vhost->ssl_ca_cert_file);
            current_vhost->ssl_ca_cert_file = strdup(value);
        }
        return 0;
    }

    if (streqi(directive, "SSLCACertificatePath")) {
        if (current_vhost) {
            safe_free(&current_vhost->ssl_ca_cert_path);
            current_vhost->ssl_ca_cert_path = strdup(value);
        }
        return 0;
    }

    if (streqi(directive, "SSLCARevocationFile")) {
        if (current_vhost) {
            safe_free(&current_vhost->ssl_ca_rev_file);
            current_vhost->ssl_ca_rev_file = strdup(value);
        }
        return 0;
    }

    if (streqi(directive, "SSLOCSPDefaultResponder")) {
        if (current_vhost) {
            safe_free(&current_vhost->ssl_ocsp_responder);
            current_vhost->ssl_ocsp_responder = strdup(value);
        }
        return 0;
    }

    if (streqi(directive, "SSLVerifyClient")) {
        if (current_vhost) {
            if (streqi(value, "require"))
                current_vhost->ssl_verify_client = 2;
            else if (streqi(value, "optional") ||
                     streqi(value, "optional_no_ca"))
                current_vhost->ssl_verify_client = 1;
            else
                current_vhost->ssl_verify_client = 0;
        }
        return 0;
    }

    if (streqi(directive, "SSLVerifyDepth")) {
        if (current_vhost) {
            char *endp;
            long v = strtol(value, &endp, 10);
            if (endp != value && v >= 0 && v <= 255)
                current_vhost->ssl_verify_depth = (int)v;
        }
        return 0;
    }

    if (streqi(directive, "SSLSessionTickets")) {
        if (current_vhost)
            current_vhost->ssl_session_tickets = strcasestr(value, "on") ? 1 : 0;
        return 0;
    }

    /* ---- Proxy ---- */

    if (streqi(directive, "ProxyPass")) {
        if (current_vhost) {
            /* Validate backend URL scheme.
             * value format: "path backend [opts]" e.g. "/api/ http://host/" */
            const char *backend = NULL;
            {
                /* Scan past the first word (path) to find backend URL */
                const char *s = value;
                while (*s && isspace((unsigned char)*s)) s++;
                /* skip path word */
                while (*s && !isspace((unsigned char)*s)) s++;
                while (*s && isspace((unsigned char)*s)) s++;
                backend = s;
            }
            if (backend && *backend) {
                if (strncmp(backend, "http://", 7) != 0 &&
                    strncmp(backend, "https://", 8) != 0 &&
                    strncmp(backend, "ws://", 5) != 0 &&
                    strncmp(backend, "wss://", 6) != 0 &&
                    strncmp(backend, "unix:", 5) != 0 &&
                    strncmp(backend, "ajp://", 6) != 0) {
                    fprintf(stderr,
                            "litehttpd-confconv: warning: ProxyPass backend has "
                            "unrecognized scheme, skipping\n");
                    return 0;
                }
            }
            str_append(&current_vhost->proxy_pass, value);
        }
        return 0;
    }

    if (streqi(directive, "ProxyPassReverse")) {
        if (current_vhost)
            str_append(&current_vhost->proxy_pass_reverse, value);
        return 0;
    }

    /* ---- Expires ---- */

    if (streqi(directive, "ExpiresActive")) {
        if (current_vhost)
            current_vhost->expires_active = strcasestr(value, "on") ? 1 : 0;
        return 0;
    }

    if (streqi(directive, "ExpiresByType")) {
        if (current_vhost) {
            /* Format: type "access plus N time" or "modification plus N time" */
            char *p2 = (char *)value;
            char *mime_type = next_word(&p2);
            if (mime_type) {
                char *dur = rest_of_line(p2);
                /* Detect modification vs access mode */
                const char *dp = dur;
                if (*dp == '"') dp++;
                while (*dp && isspace((unsigned char)*dp)) dp++;
                char mode = 'A'; /* default: access */
                if (strncasecmp(dp, "modification", 12) == 0 ||
                    *dp == 'M') {
                    mode = 'M';
                }
                long secs = ap_parse_expires_duration(dur);
                if (secs > 0) {
                    char entry[256];
                    snprintf(entry, sizeof(entry), "%s=%c%ld",
                             mime_type, mode, secs);
                    str_append(&current_vhost->expires_by_type, entry);
                }
            }
        }
        return 0;
    }

    /* ---- SuexecUserGroup ---- */

    if (streqi(directive, "SuexecUserGroup")) {
        if (current_vhost) {
            char *p2 = (char *)value;
            char *user = next_word(&p2);
            char *group = next_word(&p2);
            if (user) {
                safe_free(&current_vhost->suexec_user);
                current_vhost->suexec_user = strdup(user);
            }
            if (group) {
                safe_free(&current_vhost->suexec_group);
                current_vhost->suexec_group = strdup(group);
            }
        }
        return 0;
    }

    /* ---- ModSecurity ---- */

    if (streqi(directive, "SecRuleEngine")) {
        if (current_vhost) {
            if (strcasestr(value, "detectiononly"))
                current_vhost->modsecurity_enabled = 2;
            else if (strcasestr(value, "on"))
                current_vhost->modsecurity_enabled = 1;
            else
                current_vhost->modsecurity_enabled = 0;
        }
        return 0;
    }

    /* ---- LimitRequestBody ---- */

    if (streqi(directive, "LimitRequestBody")) {
        if (current_vhost) {
            char *endptr = NULL;
            long val = strtol(value, &endptr, 10);
            if (endptr != value && val >= 0)
                current_vhost->req_body_limit = val;
        }
        return 0;
    }

    /* ---- BrowserMatch ---- */

    if (streqi(directive, "BrowserMatch")) {
        /* No-op: OLS has no native BrowserMatch support */
        return 0;
    }

    /* ---- Alias ---- */

    if (streqi(directive, "Alias")) {
        if (current_vhost) {
            str_append(&current_vhost->aliases, value);
        }
        return 0;
    }

    /* ---- Include / IncludeOptional ---- */

    if (streqi(directive, "Include") || streqi(directive, "IncludeOptional")) {
        int is_optional = streqi(directive, "IncludeOptional");
        /* Strip quotes */
        size_t vlen = strlen(value);
        if (vlen >= 2 && value[0] == '"' && value[vlen-1] == '"') {
            value[vlen-1] = '\0'; value++;
        }
        /* Resolve relative paths */
        char resolved[AP_MAX_LINE];
        if (value[0] != '/' && config->server_root) {
            snprintf(resolved, sizeof(resolved), "%s/%s",
                     config->server_root, value);
        } else {
            strncpy(resolved, value, sizeof(resolved) - 1);
            resolved[sizeof(resolved) - 1] = '\0';
        }
        ap_process_include(resolved, is_optional, config, current_vhost, depth);
        return 0;
    }

    /* ---- Use (macro expansion) ---- */

    if (streqi(directive, "Use")) {
        if (s_macro_depth >= AP_MAX_INCLUDE_DEPTH) {
            fprintf(stderr, "litehttpd-confconv: macro expansion depth limit "
                    "reached (%d), skipping Use directive\n",
                    AP_MAX_INCLUDE_DEPTH);
            return 0;
        }
        char *p2 = (char *)value;
        char *mname = next_word(&p2);
        if (!mname) return 0;
        /* Find macro by name */
        ap_macro_t *macro = NULL;
        for (int i = 0; i < config->macro_count; i++) {
            if (config->macros[i].name &&
                strcasecmp(config->macros[i].name, mname) == 0) {
                macro = &config->macros[i];
                break;
            }
        }
        if (!macro) {
            fprintf(stderr, "litehttpd-confconv: warning: unknown macro '%s'\n",
                    mname);
            return 0;
        }
        /* Collect args with quote-aware parsing */
        char *args[AP_MAX_MACRO_PARAMS];
        int argc = 0;
        {
            char *ap = p2;
            while (*ap && argc < AP_MAX_MACRO_PARAMS) {
                while (*ap && isspace((unsigned char)*ap)) ap++;
                if (!*ap) break;
                char *arg_start;
                if (*ap == '"' || *ap == '\'') {
                    char quote = *ap++;
                    arg_start = ap;
                    while (*ap && *ap != quote) {
                        if (*ap == '\\' && ap[1]) ap++; /* skip escaped char */
                        ap++;
                    }
                    if (*ap == quote) *ap++ = '\0';
                } else {
                    arg_start = ap;
                    while (*ap && !isspace((unsigned char)*ap)) ap++;
                    if (*ap) *ap++ = '\0';
                }
                args[argc++] = arg_start;
            }
        }
        if (argc < macro->param_count) {
            fprintf(stderr, "litehttpd-confconv: warning: macro '%s' expects %d "
                    "args, got %d; padding with empty strings\n",
                    mname, macro->param_count, argc);
        } else if (argc > macro->param_count) {
            fprintf(stderr, "litehttpd-confconv: warning: macro '%s' expects %d "
                    "args, got %d; extra args ignored\n",
                    mname, macro->param_count, argc);
        }
        /* Expand body: replace $param/@param with corresponding arg */
        char *expanded = malloc(AP_MAX_MACRO_BODY);
        if (!expanded) return -1;
        const char *src = macro->body;
        int elen = 0;
        while (*src && elen < AP_MAX_MACRO_BODY - 1) {
            if (*src == '$' || *src == '@') {
                /* Check if this matches a param */
                int matched = 0;
                for (int i = 0; i < macro->param_count; i++) {
                    int plen = (int)strlen(macro->params[i]);
                    if (strncmp(src, macro->params[i], plen) == 0 &&
                        (src[plen] == '\0' || src[plen] == '\n' ||
                         isspace((unsigned char)src[plen]) ||
                         src[plen] == '/' || src[plen] == '"' ||
                         src[plen] == '>' || src[plen] == ')' ||
                         src[plen] == ',' || src[plen] == ';' ||
                         src[plen] == '=')) {
                        const char *replacement = (i < argc) ? args[i] : "";
                        int alen = (int)strlen(replacement);
                        if (elen + alen < AP_MAX_MACRO_BODY - 1) {
                            memcpy(expanded + elen, replacement, alen);
                            elen += alen;
                        }
                        src += plen;
                        matched = 1;
                        break;
                    }
                }
                if (!matched)
                    expanded[elen++] = *src++;
            } else {
                expanded[elen++] = *src++;
            }
        }
        expanded[elen] = '\0';
        /* Re-parse expanded text via stream so block directives work */
        s_macro_depth++;
        FILE *mem_fp = fmemopen(expanded, elen, "r");
        if (mem_fp) {
            ap_parse_stream(mem_fp, config, current_vhost, depth + 1);
            fclose(mem_fp);
        } else {
            /* Fallback: tmpfile */
            FILE *tmp_fp = tmpfile();
            if (tmp_fp) {
                fwrite(expanded, 1, elen, tmp_fp);
                rewind(tmp_fp);
                ap_parse_stream(tmp_fp, config, current_vhost, depth + 1);
                fclose(tmp_fp);
            }
        }
        s_macro_depth--;
        free(expanded);
        return 0;
    }

    /* ---- Closing tags for transparent blocks ---- */
    if (directive[0] == '<' || streqi(directive, "</IfModule>") ||
        streqi(directive, "</IfDefine>")) {
        return 0;
    }

    /* Unknown directive: silently skip */
    return 0;
}

/* ---- stream parser ---- */

static int ap_parse_stream(FILE *fp, ap_config_t *config,
                           ap_vhost_t *current_vhost, int depth) {
    char line[AP_MAX_LINE];
    int error_count = 0;
    while (fgets(line, sizeof(line), fp)) {
        /* Fix 7: Detect truncated lines (no newline and not EOF) */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] != '\n' && !feof(fp)) {
            /* Line was truncated -- drain the rest */
            fprintf(stderr, "litehttpd-confconv: warning: line too long "
                    "(>%d bytes), truncated\n", AP_MAX_LINE - 1);
            int ch;
            while ((ch = fgetc(fp)) != EOF && ch != '\n')
                ; /* drain */
        }

        char *trimmed = trim(line);
        if (!trimmed[0] || trimmed[0] == '#') continue;

        /* Closing tags at top level */
        if (trimmed[0] == '<' && trimmed[1] == '/') continue;

        /* Fix 8: Check return value of ap_parse_line */
        int rc = ap_parse_line(trimmed, config, current_vhost, fp, depth);
        if (rc < 0) {
            error_count++;
            if (error_count > 100) {
                fprintf(stderr, "litehttpd-confconv: too many parse errors, "
                        "aborting\n");
                return -1;
            }
        }
    }
    return error_count > 0 ? -1 : 0;
}

/* ---- public API ---- */

int ap_parse_config(const char *path, ap_config_t *config) {
    if (!path || !config) return -1;
    memset(config, 0, sizeof(*config));

    config->panel_type = ap_detect_panel(path);

    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "litehttpd-confconv: cannot stat %s: %s\n",
                path, strerror(errno));
        return -1;
    }
    if (st.st_size > AP_MAX_FILE_SIZE) {
        fprintf(stderr, "litehttpd-confconv: file too large: %s (%ld bytes)\n",
                path, (long)st.st_size);
        return -1;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "litehttpd-confconv: cannot open %s: %s\n",
                path, strerror(errno));
        return -1;
    }

    int rc = ap_parse_stream(fp, config, NULL, 0);
    fclose(fp);
    return rc;
}

void ap_config_free(ap_config_t *config) {
    if (!config) return;
    for (int i = 0; i < config->vhost_count; i++)
        ap_vhost_free(&config->vhosts[i]);
    for (int i = 0; i < config->macro_count; i++) {
        safe_free(&config->macros[i].name);
        for (int j = 0; j < config->macros[i].param_count; j++)
            safe_free(&config->macros[i].params[j]);
        safe_free(&config->macros[i].body);
    }
    safe_free(&config->server_root);
    safe_free(&config->server_name);
    safe_free(&config->server_admin);
    safe_free(&config->doc_root);
    safe_free(&config->user);
    safe_free(&config->group);
}
