#ifndef HTACCESS_FIREWALL_CLIENT_H
#define HTACCESS_FIREWALL_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#define FW_SOCK_PATH "/var/run/litehttpd/firewall.sock"
#define FW_CMD_MAX   128

/* Initialize the firewall client (connect to daemon).
 * Returns 0 on success, -1 if daemon is unavailable. */
int fw_client_init(void);

/* Send a block command for an IP.
 * Returns 0 on success, -1 on failure (daemon down, write error). */
int fw_client_block(const char *ip, int ttl_seconds);

/* Send an unblock command for an IP. */
int fw_client_unblock(const char *ip);

/* Check if the firewall daemon is reachable. */
int fw_client_is_available(void);

/* Close the connection (call on module cleanup). */
void fw_client_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
