#ifndef HTACCESS_CIDR_TABLE_H
#define HTACCESS_CIDR_TABLE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CIDR_TABLE_MAX_V4  1024
#define CIDR_TABLE_MAX_V6   256

typedef struct {
    uint32_t network;   /* pre-masked network address */
    uint32_t mask;
} cidr_range_v4_t;

typedef struct {
    uint8_t network[16];
    uint8_t mask[16];   /* full 16-byte mask */
    uint8_t prefix;     /* for display/logging */
} cidr_range_v6_t;

typedef struct {
    cidr_range_v4_t v4[CIDR_TABLE_MAX_V4];
    size_t          v4_count;
    cidr_range_v6_t v6[CIDR_TABLE_MAX_V6];
    size_t          v6_count;
} cidr_table_t;

/* Build: returns 0 on success, -1 on bad entry or overflow (skips bad). */
int cidr_table_add_v4(cidr_table_t *t, uint32_t network, uint32_t mask);
int cidr_table_add_v6(cidr_table_t *t, const uint8_t addr[16], int prefix);
int cidr_table_add_str(cidr_table_t *t, const char *cidr_str); /* auto v4/v6 */

/* Query: returns 1 if match, 0 if no match. O(log n) for v4 after sort. */
int cidr_table_match_v4(const cidr_table_t *t, uint32_t ip);
int cidr_table_match_v6(const cidr_table_t *t, const uint8_t addr[16]);

/* Sort after all adds (must call before querying). */
void cidr_table_finalize(cidr_table_t *t);

/* Convenience: is the table empty? */
int cidr_table_is_empty(const cidr_table_t *t);

#ifdef __cplusplus
}
#endif
#endif
