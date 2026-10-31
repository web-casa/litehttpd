# Spec: pre-compiled CIDR table with binary search

## Problem
htaccess_exec_require.c:39-93 does per-request strdup + strtok_r + cidr_parse
for each CIDR in each Require directive. With N rules and M CIDRs per rule,
cost is O(N*M) per request with string allocation overhead.

## Solution
Pre-compile CIDR lists at directive-parse time into a sorted array; at match
time use binary search. IPv4 and IPv6 in separate arrays (IPv4 as sorted
uint32 network ranges, IPv6 as 128-bit network ranges).

## Requirements
1. `cidr_table_t`: opaque compiled set, created once, queried many times.
2. IPv4: sort by network address; binary search on (ip & mask) == network.
3. IPv6: same but with 16-byte arrays.
4. Table caches per directive (in htaccess_cache, alongside parsed directives).
5. Performance target: 10K rules, linear scan of pre-masked entries (O(n) but ~100x faster than per-request string parsing; binary search with overlapping masks is P10 scope), < 1µs per request.
6. `cidr_table_add_*()` during parse; `cidr_table_match_*()` at runtime.

## References
- Apache mod_authz_core IP matching (behavioral reference)
- Existing cidr_parse/cidr_match in htaccess_cidr.c (reuse for parse-time)

## Integration
Replace `ip_in_cidr_list_v4()` in htaccess_exec_require.c with table lookup.
