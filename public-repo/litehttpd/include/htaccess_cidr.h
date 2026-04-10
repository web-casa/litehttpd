/**
 * htaccess_cidr.h - CIDR parsing and matching for OLS .htaccess module
 *
 * Provides IPv4 CIDR notation parsing ("A.B.C.D/N") and IP-in-range
 * matching. Also supports the "all" keyword to match any IP address.
 *
 * All addresses and masks are stored in host byte order.
 *
 * Validates: Requirements 6.3, 6.4, 6.5
 */
#ifndef HTACCESS_CIDR_H
#define HTACCESS_CIDR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * IPv4 CIDR range — network address and subnet mask in host byte order.
 */
typedef struct {
    uint32_t network;   /* Network address (host byte order) */
    uint32_t mask;      /* Subnet mask (host byte order)     */
} cidr_v4_t;

/**
 * Parse a CIDR string into a cidr_v4_t structure.
 *
 * Supported formats:
 *   - "A.B.C.D/N"  — CIDR notation, N in [0..32]
 *   - "A.B.C.D"    — plain IP, treated as /32
 *   - "all"         — matches everything (network=0, mask=0)
 *
 * The resulting network is masked: out->network = ip & out->mask.
 *
 * @param cidr_str  Input string (must not be NULL).
 * @param out       Output structure (must not be NULL).
 * @return 0 on success, -1 on error (invalid format / out-of-range).
 */
int cidr_parse(const char *cidr_str, cidr_v4_t *out);

/**
 * Check whether an IP address falls within a CIDR range.
 *
 * @param cidr  Parsed CIDR range.
 * @param ip    IPv4 address in host byte order.
 * @return 1 if the IP matches, 0 otherwise.
 *
 * A mask of 0 (from "all") matches every IP.
 */
int cidr_match(const cidr_v4_t *cidr, uint32_t ip);

/**
 * Parse a dotted-decimal IPv4 string into a uint32_t in host byte order.
 *
 * @param ip_str   Input string (e.g. "192.168.1.100").
 * @param out_ip   Output value in host byte order.
 * @return 0 on success, -1 on error.
 */
int ip_parse(const char *ip_str, uint32_t *out_ip);

/* ------------------------------------------------------------------ */
/*  IPv6 CIDR support                                                  */
/* ------------------------------------------------------------------ */

/**
 * IPv6 CIDR range — 128-bit network address and prefix length.
 */
typedef struct {
    uint8_t addr[16];   /* Network address (network byte order) */
    int     prefix_len; /* Prefix length [0..128] */
} cidr_v6_t;

/**
 * Parse an IPv6 address string into a 16-byte array.
 *
 * Uses inet_pton(AF_INET6, ...) internally.
 *
 * @param text  IPv6 address string (e.g. "::1", "2001:db8::1").
 * @param out   Output 16-byte array (network byte order).
 * @return 0 on success, -1 on error.
 */
int ip_parse_v6(const char *text, uint8_t out[16]);

/**
 * Parse an IPv6 CIDR string into a cidr_v6_t structure.
 *
 * Supported formats:
 *   - "addr/N"  — CIDR notation, N in [0..128]
 *   - "addr"    — plain IPv6, treated as /128
 *
 * @param text  Input string.
 * @param out   Output structure.
 * @return 0 on success, -1 on error.
 */
int cidr_v6_parse(const char *text, cidr_v6_t *out);

/**
 * Check whether an IPv6 address falls within a CIDR range.
 *
 * @param cidr  Parsed CIDR range.
 * @param addr  IPv6 address (16 bytes, network byte order).
 * @return 1 if the address matches, 0 otherwise.
 */
int cidr_v6_match(const cidr_v6_t *cidr, const uint8_t addr[16]);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_CIDR_H */
