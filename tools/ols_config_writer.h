/*
 * ols_config_writer.h -- OLS config generator
 *
 * Part of litehttpd-confconv: Apache -> OpenLiteSpeed config converter.
 */
#ifndef OLS_CONFIG_WRITER_H
#define OLS_CONFIG_WRITER_H

#include "apacheconf_parser.h"

/* Write OLS configuration files from parsed Apache config.
 * Creates output_dir/vhosts/<name>/vhconf.conf for each vhost.
 * Returns 0 on success.
 */
int ols_write_config(const ap_config_t *config, const char *output_dir);

/* Write a single vhost config file. */
int ols_write_vhost(const ap_vhost_t *vhost, const char *filepath);

/* Detect best available lsphp binary.
 * handler_hint may be NULL; returns static string or NULL.
 */
const char *ols_detect_php(const char *handler_hint);

/* Map Apache SSLProtocol string to OLS bitmask.
 * Returns OLS sslProtocol numeric value.
 */
int ols_map_ssl_protocol(const char *proto);

#endif /* OLS_CONFIG_WRITER_H */
