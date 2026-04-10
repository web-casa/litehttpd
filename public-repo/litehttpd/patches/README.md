# OLS Custom Binary Patches

Minimal patches for OpenLiteSpeed to enable features that cannot be
implemented via the LSIAPI module interface alone.

## Why Custom OLS?

Stock OLS has a hard limitation: `g_api->set_req_env("PHP_VALUE", ...)` sets
a request environment variable, but OLS's LSAPI connector only forwards PHP
configuration through `PHPConfig::buildLsapiEnv()`. Without patching OLS,
`php_value` / `php_flag` directives in `.htaccess` have no effect.

Our module (`litehttpd_htaccess.so`) already has runtime detection:
- If `g_api->set_php_config_value != NULL` → uses native API (patched OLS)
- If NULL → falls back to env var (stock OLS, php_value won't work)

## Patch Contents

### Patch 1: PHPConfig LSIAPI Extensions

**Files modified:**
- `include/ls.h` — 4 new function pointers at end of `lsi_api_t`
- `src/lsiapi/lsiapilib.cpp` — Implementation using existing PHPConfig class

**New API functions:**

| Function | Purpose |
|----------|---------|
| `set_php_config_value(session, name, value, type)` | Set php_value/php_admin_value |
| `set_php_config_flag(session, name, value, type)` | Set php_flag/php_admin_flag |
| `get_php_config(session, name, buf, buflen)` | Read back (stub, rarely needed) |
| `set_req_header(session, name, nlen, val, vlen, op)` | Modify request headers (future) |

**Type constants** (from `src/http/phpconfig.h`):
- `PHP_VALUE = 1` (php_value)
- `PHP_FLAG = 2` (php_flag)
- `PHP_ADMIN_VALUE = 3` (php_admin_value)
- `PHP_ADMIN_FLAG = 4` (php_admin_flag)

**Internal flow:**
```
module calls g_api->set_php_config_value(session, "upload_max_filesize", "128M", 1)
  → lambda in lsiapilib.cpp
  → HttpSession → HttpReq → HttpContext::getPHPConfig()
  → new PHPConfig() if needed, setPHPConfig()
  → PHPConfig::parse(type, "upload_max_filesize 128M", err, errlen)
  → PHPConfig::buildLsapiEnv()
  → LsapiReq::appendSpecialEnv() reads PHPConfig → LSAPI protocol → lsphp
```

## Applying

```bash
cd /path/to/openlitespeed-1.8.5
patch -p1 < /path/to/litehttpd/patches/0001-lsiapi-phpconfig.patch
```

## Building

```bash
./configure --prefix=/usr/local/lsws
make -j$(nproc)
make install
```

## Compatibility

- Patch appends to the END of `lsi_api_t` — ABI compatible with all existing modules
- Stock OLS zero-initializes the struct → new pointers are NULL → old modules unaffected
- Tested against OLS v1.8.5 source (January 2025)
- PHPConfig class already exists in OLS source — no new classes needed
