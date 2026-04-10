/*
 * litehttpd-confconv.c -- CLI entry point for Apache -> OLS config converter
 *
 * Usage: litehttpd-confconv --input <apache-conf> --output <ols-conf-dir> [portmap=from:to,...]
 *        litehttpd-confconv --check <apache-conf> --state <state-file>
 *        litehttpd-confconv --watch <apache-conf> --interval <secs> --output <dir>
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "apacheconf_parser.h"
#include "ols_config_writer.h"

static volatile sig_atomic_t g_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s --input <apache-conf> --output <ols-conf-dir> "
        "[portmap=from:to,...]\n"
        "       %s --check <apache-conf> --state <state-file>\n"
        "       %s --watch <apache-conf> --interval <secs> --output <dir>\n"
        "\n"
        "Convert Apache httpd.conf to OpenLiteSpeed configuration.\n"
        "\n"
        "Options:\n"
        "  --input  <path>     Path to Apache httpd.conf\n"
        "  --output <dir>      Output directory for OLS config files\n"
        "  portmap=F:T[,F:T]   Map Apache ports to OLS ports\n"
        "                      (e.g., portmap=80:8088,443:8443)\n"
        "  --check <path>      Check if config file has changed\n"
        "  --state <path>      State file for --check mode\n"
        "  --watch <path>      Watch config for changes and re-convert\n"
        "  --interval <secs>   Poll interval for --watch mode (default: 60)\n"
        "\n"
        "Output files:\n"
        "  <dir>/listeners.conf         Listener definitions\n"
        "  <dir>/vhosts.conf            VirtualHost registrations\n"
        "  <dir>/vhosts/<name>/vhconf.conf  Per-vhost configuration\n"
        "\n"
        "Exit codes (--check mode):\n"
        "  0  Config has changed\n"
        "  1  Config is unchanged\n"
        "  2  Error\n",
        prog, prog, prog);
}

int main(int argc, char *argv[]) {
    const char *input = NULL;
    const char *output = NULL;
    const char *portmap = NULL;
    const char *check_path = NULL;
    const char *state_file = NULL;
    const char *watch_path = NULL;
    int watch_interval = 60;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strncmp(argv[i], "portmap=", 8) == 0) {
            portmap = argv[i] + 8;
        } else if (strcmp(argv[i], "--check") == 0 && i + 1 < argc) {
            check_path = argv[++i];
        } else if (strcmp(argv[i], "--state") == 0 && i + 1 < argc) {
            state_file = argv[++i];
        } else if (strcmp(argv[i], "--watch") == 0 && i + 1 < argc) {
            watch_path = argv[++i];
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            watch_interval = atoi(argv[++i]);
            if (watch_interval < 1) watch_interval = 60;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    /* ---- Check mode ---- */
    if (check_path) {
        if (!state_file) {
            fprintf(stderr, "Error: --state is required with --check.\n\n");
            usage(argv[0]);
            return 2;
        }
        int rc = ap_check_config_changed(check_path, state_file);
        if (rc < 0) {
            fprintf(stderr, "Error: cannot stat %s\n", check_path);
            return 2;
        }
        if (rc == 1) {
            fprintf(stdout, "CHANGED\n");
            return 0;
        } else {
            fprintf(stdout, "UNCHANGED\n");
            return 1;
        }
    }

    /* ---- Watch mode ---- */
    if (watch_path) {
        if (!output) {
            fprintf(stderr, "Error: --output is required with --watch.\n\n");
            usage(argv[0]);
            return 1;
        }
        /* Use a temporary state file */
        char watch_state[4096];
        snprintf(watch_state, sizeof(watch_state),
                 "%s/.confconv-watch.state", output);

        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = handle_signal;
        sa.sa_flags = SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);

        fprintf(stdout, "litehttpd-confconv: watching %s every %d seconds\n",
                watch_path, watch_interval);

        while (g_running) {
            int changed = ap_check_config_changed_no_save(watch_path,
                                                          watch_state);
            if (changed == 1) {
                fprintf(stdout, "litehttpd-confconv: config changed, re-converting...\n");
                ap_config_t config;
                if (ap_parse_config(watch_path, &config) == 0) {
                    if (portmap) {
                        char buf[256];
                        strncpy(buf, portmap, sizeof(buf) - 1);
                        buf[sizeof(buf) - 1] = '\0';
                        char *saveptr = NULL;
                        char *tok = strtok_r(buf, ",", &saveptr);
                        while (tok && config.port_map_count < AP_MAX_PORTMAP) {
                            int from = 0, to = 0;
                            if (sscanf(tok, "%d:%d", &from, &to) == 2 &&
                                from > 0 && to > 0) {
                                config.port_from[config.port_map_count] = from;
                                config.port_to[config.port_map_count] = to;
                                config.port_map_count++;
                            }
                            tok = strtok_r(NULL, ",", &saveptr);
                        }
                    }
                    if (ols_write_config(&config, output) == 0) {
                        /* Only update state after successful parse+write */
                        ap_save_config_state(watch_path, watch_state);
                    }
                    ap_config_free(&config);
                } else {
                    fprintf(stderr,
                            "litehttpd-confconv: failed to parse %s\n", watch_path);
                }
            }
            for (int s = 0; s < watch_interval && g_running; s++)
                sleep(1);
        }

        fprintf(stdout, "litehttpd-confconv: watch stopped\n");
        return 0;
    }

    /* ---- Normal convert mode ---- */
    if (!input || !output) {
        fprintf(stderr, "Error: --input and --output are required.\n\n");
        usage(argv[0]);
        return 1;
    }

    /* Parse Apache config */
    ap_config_t config;
    if (ap_parse_config(input, &config) != 0) {
        fprintf(stderr, "Error: failed to parse %s\n", input);
        return 1;
    }

    /* Apply portmap if provided */
    if (portmap) {
        char buf[256];
        strncpy(buf, portmap, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char *saveptr = NULL;
        char *tok = strtok_r(buf, ",", &saveptr);
        while (tok && config.port_map_count < AP_MAX_PORTMAP) {
            int from = 0, to = 0;
            if (sscanf(tok, "%d:%d", &from, &to) == 2 &&
                from > 0 && to > 0) {
                config.port_from[config.port_map_count] = from;
                config.port_to[config.port_map_count] = to;
                config.port_map_count++;
            }
            tok = strtok_r(NULL, ",", &saveptr);
        }
    }

    /* Write OLS config */
    int rc = ols_write_config(&config, output);

    /* Summary */
    if (rc == 0) {
        fprintf(stdout, "Summary: %d vhost(s) imported",
                config.vhost_count);
        if (config.listen_count > 0)
            fprintf(stdout, ", %d listen port(s)", config.listen_count);
        if (config.port_map_count > 0) {
            fprintf(stdout, ", port mapping:");
            for (int i = 0; i < config.port_map_count; i++)
                fprintf(stdout, " %d->%d",
                        config.port_from[i], config.port_to[i]);
        }
        fprintf(stdout, "\n");
    }

    ap_config_free(&config);
    return rc;
}
