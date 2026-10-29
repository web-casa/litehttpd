# Spec: ABI version handshake for custom OLS patches

## Problem
The hand-maintained `lsi_api_t` stub in `include/ls.h` appends private fields
(set_php_config_value, set_req_header, parse_rewrite_rules, etc.) after the
stock OLS public fields. On stock OLS, the actual struct is shorter — reading
these appended fields is undefined behavior (reads past the allocation).

## Solution
Replace the bare `dlsym` marker with a versioned ABI info block exported by
patched OLS builds. The module validates magic, version, and struct size
before touching any extension field.

### ABI info block (exported by patched OLS as `litehttpd_abi_info`)
```c
typedef struct {
    uint32_t magic;           /* 0x4C485444 ("LHTD") */
    uint32_t version;         /* 1 */
    uint32_t api_struct_size; /* sizeof(lsi_api_t) in the patched build */
    uint32_t feature_bitmap;  /* bit0: PHP config, bit1: rewrite, bit2: req_header */
} litehttpd_abi_info_t;
```

### Safety rules
1. Module reads `g_api` only after confirming stock size OR matching ABI info.
2. Extension field access requires: `abi_info->magic == 0x4C485444` AND
   `abi_info->version >= 1` AND `abi_info->api_struct_size >= offsetof(
   lsi_api_t, parse_rewrite_rules) + sizeof(void*)`.
3. Feature-specific checks use the bitmap, not NULL function pointers alone.

## References
- OLS source (GPLv3) — LSI API struct layout: `include/ls.h` in openlitespeed
- Observation: stock OLS 1.9.1 prebuilt, lsiapi_shim.c dlsym check

## Patch updates required
- patch 0001: export `litehttpd_abi_info` with PHP_CONFIG + REQ_HEADER bits
- patch 0002: export with REWRITE bit
- Both patches: bump `api_struct_size` to actual `sizeof(lsi_api_t)`
