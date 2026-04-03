---
title: OLS Patches
description: Optional patches for OpenLiteSpeed to enable advanced features
---

These patches are what distinguish LiteHTTPD-Full from LiteHTTPD-Thin. LiteHTTPD works with stock OLS (Thin edition), but these 4 patches enable additional features that make up the Full edition.

## Patch Overview

| Patch | Feature | Required For |
|-------|---------|-------------|
| 0001 | PHPConfig LSIAPI | php_value, php_flag directives |
| 0002 | RewriteRule LSIAPI | RewriteRule execution (not just parsing) |
| 0003 | readApacheConf | Auto-convert Apache config at startup |
| 0004 | autoIndex 403 | Options -Indexes returns 403 |

## Patch 0001: PHPConfig LSIAPI Extensions

Adds function pointers to `lsi_api_t` for PHP configuration:

```c
int (*set_php_config_value)(session, name, value, type);
int (*set_php_config_flag)(session, name, int_value, type);
int (*get_php_config)(session, name, buf, buf_len);
int (*set_req_header)(session, name, name_len, value, value_len, op);
```

**Files modified**: `include/ls.h`, `src/lsiapi/lsiapilib.cpp`

Without this patch, `php_value` and `php_flag` directives are parsed but cannot be passed to lsphp.

## Patch 0002: RewriteRule LSIAPI Extensions

Adds rewrite engine access to the module:

```c
void *(*parse_rewrite_rules)(rules_text, text_len);
int (*exec_rewrite_rules)(session, handle, base, base_len);
void (*free_rewrite_rules)(handle);
```

Also adds `getStatusCode()` public getter to `RewriteEngine` class.

**Files modified**: `include/ls.h`, `src/http/rewriteengine.h`, `src/lsiapi/lsiapilib.cpp`

Without this patch, RewriteRule directives are parsed but not executed. The module falls back to OLS native `RewriteFile .htaccess` processing.

## Patch 0003: readApacheConf Startup Hook

Adds a `readApacheConf` directive to OLS plainconf that triggers `litehttpd-confconv` at startup:

```
readApacheConf /etc/httpd/conf/httpd.conf portmap=80:8088,443:8443
```

**Files modified**: `src/main/plainconf.cpp`

## Patch 0004: autoIndex 403

When `.htaccess` contains `Options -Indexes` and a directory has no index file, this patch returns 403 Forbidden instead of showing a directory listing or returning 404.

**Files modified**: `src/http/httpreq.cpp`

The patch scans `.htaccess` in `HttpReq::processPath()` at the contextMap stage, parsing `Options` tokens with proper `+Indexes`/`-Indexes` precedence.

## Applying Patches

```bash
cd /path/to/openlitespeed-1.8.5
patch -p1 < /path/to/litehttpd/patches/0001-lsiapi-phpconfig.patch
patch -p1 < /path/to/litehttpd/patches/0002-lsiapi-rewrite.patch
# Patch 0003 and 0004 require manual application (Python scripts)
bash build.sh
```

## ABI Compatibility

All patches append to the end of existing structures -- they never modify existing field offsets. This ensures:
- Modules compiled against patched headers work with patched OLS
- Stock OLS modules continue to work (new fields are simply unused)
- The module detects patch availability at runtime via NULL pointer checks
