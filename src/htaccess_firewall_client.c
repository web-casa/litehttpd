/**
 * htaccess_firewall_client.c — UDS client for litehttpd-firewalld daemon.
 *
 * Sends block/unblock commands to the Rust firewall daemon over a Unix
 * domain socket. The protocol is line-based: "b,<ip>[,ttl]\n" to block,
 * "u,<ip>\n" to unblock; the daemon answers "ok\n" or "err,<reason>\n".
 *
 * Design constraints:
 *  - This runs on the request path (brute-force threshold escalation), so a
 *    wedged daemon must never wedge the server: every command opens a fresh
 *    connection with a 100ms send/receive timeout. On any failure the caller
 *    falls back to the ordinary application-level block.
 *  - A fresh connection per command also survives daemon restarts without
 *    stale-fd bookkeeping; UDS connect cost is negligible for an event that
 *    fires once per blocked attacker, not per request.
 */
#include "htaccess_firewall_client.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define FW_IO_TIMEOUT_MS 100

static const char *fw_sock_path(void)
{
    /* Same override the Rust daemon honours (LITEHTTPD_FW_SOCK); lets tests
     * point both sides at a temp path without root privileges. */
    const char *env = getenv("LITEHTTPD_FW_SOCK");
    return (env && env[0]) ? env : FW_SOCK_PATH;
}

static int fw_connect_once(void)
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", fw_sock_path());

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct timeval tv = {
        .tv_sec = FW_IO_TIMEOUT_MS / 1000,
        .tv_usec = (FW_IO_TIMEOUT_MS % 1000) * 1000,
    };
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/**
 * Probe whether the firewall daemon socket is present and accepting
 * connections. Cheap enough for an init-time capability check.
 */
int fw_client_init(void)
{
    int fd = fw_connect_once();
    if (fd < 0)
        return -1;
    close(fd);
    return 0;
}

int fw_client_is_available(void)
{
    return fw_client_init() == 0;
}

static int fw_send(const char *cmd, size_t cmd_len)
{
    int fd = fw_connect_once();
    if (fd < 0)
        return -1;

    int rc = -1;
    ssize_t sent = write(fd, cmd, cmd_len);
    if (sent == (ssize_t)cmd_len) {
        char resp[64];
        ssize_t r = read(fd, resp, sizeof(resp) - 1);
        if (r > 0) {
            resp[r] = '\0';
            rc = (strncmp(resp, "ok", 2) == 0) ? 0 : -1;
        }
    }
    close(fd);
    return rc;
}

int fw_client_block(const char *ip, int ttl_seconds)
{
    if (!ip || !ip[0])
        return -1;

    char cmd[FW_CMD_MAX];
    int n;
    if (ttl_seconds > 0)
        n = snprintf(cmd, sizeof(cmd), "b,%s,%d\n", ip, ttl_seconds);
    else
        n = snprintf(cmd, sizeof(cmd), "b,%s\n", ip);

    if (n <= 0 || n >= (int)sizeof(cmd))
        return -1;

    return fw_send(cmd, (size_t)n);
}

int fw_client_unblock(const char *ip)
{
    if (!ip || !ip[0])
        return -1;

    char cmd[FW_CMD_MAX];
    int n = snprintf(cmd, sizeof(cmd), "u,%s\n", ip);
    if (n <= 0 || n >= (int)sizeof(cmd))
        return -1;

    return fw_send(cmd, (size_t)n);
}

void fw_client_shutdown(void)
{
    /* Stateless since 1.1: connections are per-command, nothing to close. */
}
