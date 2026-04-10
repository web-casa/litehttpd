/**
 * htaccess_cidr.c - CIDR parsing and matching implementation
 *
 * Validates: Requirements 6.3, 6.4, 6.5
 */
#include "htaccess_cidr.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <arpa/inet.h>

/**
 * Internal: parse four dotted-decimal octets from `str` into a uint32_t
 * in host byte order.  On success sets *out and returns 0; on any format
 * error returns -1.
 *
 * `end` (if not NULL) is set to point to the first character after the
 * last octet that was consumed.
 */
static int parse_ipv4(const char *str, uint32_t *out, const char **end)
{
    uint32_t addr = 0;
    int i;

    for (i = 0; i < 4; i++) {
        const char *p = str;
        unsigned long octet;
        char *ep;

        if (i > 0) {
            if (*p != '.')
                return -1;
            p++;
        }

        /* Reject leading zeros (e.g. "01") to avoid ambiguity */
        if (*p == '0' && isdigit((unsigned char)p[1]))
            return -1;

        errno = 0;
        octet = strtoul(p, &ep, 10);
        if (ep == p || octet > 255 || errno == ERANGE)
            return -1;

        addr = (addr << 8) | (uint32_t)octet;
        str = ep;
    }

    *out = addr;
    if (end)
        *end = str;
    return 0;
}

/**
 * Build a subnet mask for a given prefix length (0-32).
 * E.g. prefix=24 → 0xFFFFFF00
 */
static uint32_t prefix_to_mask(int prefix)
{
    if (prefix == 0)
        return 0;
    return ~((uint32_t)0) << (32 - prefix);
}

/* ------------------------------------------------------------------ */

int cidr_parse(const char *cidr_str, cidr_v4_t *out)
{
    const char *p;
    uint32_t ip;
    int prefix;

    if (!cidr_str || !out)
        return -1;

    /* Skip leading whitespace */
    while (*cidr_str && isspace((unsigned char)*cidr_str))
        cidr_str++;

    /* Handle "all" keyword (case-insensitive) */
    if (strncasecmp(cidr_str, "all", 3) == 0) {
        p = cidr_str + 3;
        /* Only trailing whitespace allowed */
        while (*p && isspace((unsigned char)*p))
            p++;
        if (*p != '\0')
            return -1;
        out->network = 0;
        out->mask    = 0;
        return 0;
    }

    /* Parse the IP portion */
    if (parse_ipv4(cidr_str, &ip, &p) != 0)
        return -1;

    if (*p == '/') {
        /* CIDR notation: parse prefix length */
        char *ep;
        unsigned long val;

        p++;  /* skip '/' */
        val = strtoul(p, &ep, 10);
        if (ep == p || val > 32)
            return -1;
        prefix = (int)val;
        p = ep;
    } else {
        /* Plain IP — treat as /32 */
        prefix = 32;
    }

    /* Only trailing whitespace allowed */
    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p != '\0')
        return -1;

    out->mask    = prefix_to_mask(prefix);
    out->network = ip & out->mask;
    return 0;
}

int cidr_match(const cidr_v4_t *cidr, uint32_t ip)
{
    if (!cidr)
        return 0;
    /* mask==0 (from "all") matches everything */
    return (ip & cidr->mask) == (cidr->network & cidr->mask) ? 1 : 0;
}

int ip_parse(const char *ip_str, uint32_t *out_ip)
{
    const char *end;

    if (!ip_str || !out_ip)
        return -1;

    /* Skip leading whitespace */
    while (*ip_str && isspace((unsigned char)*ip_str))
        ip_str++;

    if (parse_ipv4(ip_str, out_ip, &end) != 0)
        return -1;

    /* Only trailing whitespace allowed */
    while (*end && isspace((unsigned char)*end))
        end++;
    if (*end != '\0')
        return -1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  IPv6 CIDR support                                                  */
/* ------------------------------------------------------------------ */

int ip_parse_v6(const char *text, uint8_t out[16])
{
    if (!text || !out)
        return -1;

    /* Skip leading whitespace */
    while (*text && isspace((unsigned char)*text))
        text++;

    /* Make a trimmed copy (inet_pton doesn't tolerate trailing whitespace) */
    char buf[64];
    size_t len = strlen(text);
    /* Trim trailing whitespace */
    while (len > 0 && isspace((unsigned char)text[len - 1]))
        len--;
    if (len == 0 || len >= sizeof(buf))
        return -1;
    memcpy(buf, text, len);
    buf[len] = '\0';

    struct in6_addr addr;
    if (inet_pton(AF_INET6, buf, &addr) != 1)
        return -1;

    memcpy(out, addr.s6_addr, 16);
    return 0;
}

int cidr_v6_parse(const char *text, cidr_v6_t *out)
{
    if (!text || !out)
        return -1;

    /* Skip leading whitespace */
    while (*text && isspace((unsigned char)*text))
        text++;

    /* Find the slash separator */
    const char *slash = strchr(text, '/');
    if (slash) {
        /* Extract address part */
        size_t addr_len = (size_t)(slash - text);
        char addr_buf[64];
        if (addr_len == 0 || addr_len >= sizeof(addr_buf))
            return -1;
        memcpy(addr_buf, text, addr_len);
        addr_buf[addr_len] = '\0';

        if (ip_parse_v6(addr_buf, out->addr) != 0)
            return -1;

        /* Parse prefix length */
        const char *p = slash + 1;
        char *ep;
        unsigned long val = strtoul(p, &ep, 10);
        if (ep == p || val > 128)
            return -1;

        /* Only trailing whitespace allowed */
        while (*ep && isspace((unsigned char)*ep))
            ep++;
        if (*ep != '\0')
            return -1;

        out->prefix_len = (int)val;
    } else {
        /* Plain address — treat as /128 */
        if (ip_parse_v6(text, out->addr) != 0)
            return -1;
        out->prefix_len = 128;
    }

    /* Mask off host bits for consistent network address */
    int full_bytes = out->prefix_len / 8;
    int remaining_bits = out->prefix_len % 8;
    for (int i = full_bytes; i < 16; i++) {
        if (i == full_bytes && remaining_bits > 0) {
            uint8_t mask = (uint8_t)(0xFF << (8 - remaining_bits));
            out->addr[i] &= mask;
        } else {
            out->addr[i] = 0;
        }
    }

    return 0;
}

int cidr_v6_match(const cidr_v6_t *cidr, const uint8_t addr[16])
{
    if (!cidr || !addr)
        return 0;

    int full_bytes = cidr->prefix_len / 8;
    int remaining_bits = cidr->prefix_len % 8;

    /* Compare full bytes */
    for (int i = 0; i < full_bytes; i++) {
        if (cidr->addr[i] != addr[i])
            return 0;
    }

    /* Compare remaining bits in the partial byte */
    if (remaining_bits > 0 && full_bytes < 16) {
        uint8_t mask = (uint8_t)(0xFF << (8 - remaining_bits));
        if ((cidr->addr[full_bytes] & mask) != (addr[full_bytes] & mask))
            return 0;
    }

    return 1;
}
