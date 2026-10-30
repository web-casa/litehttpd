---
title: Editions
description: LiteHTTPD-Thin vs LiteHTTPD-Full — choose the right edition
---

LiteHTTPD comes in two editions. Both are free and open source.

## Comparison

| | LiteHTTPD-Thin | LiteHTTPD-Full |
|-|---------------|----------------|
| **What it is** | `litehttpd_htaccess.so` module only | Module + Patched OpenLiteSpeed |
| **Install on** | Stock (unmodified) OLS | One RPM from rpms.litehttpd.com |
| **Install time** | 1 minute (copy .so) | ~1 minute via RPM repo |
| **OLS binary** | Unchanged | Replaced with patched version (module bundled) |

## Feature Comparison

| Feature | LiteHTTPD-Thin | LiteHTTPD-Full |
|---------|---------------|----------------|
| Header set/unset/append/merge/add/edit | Yes | Yes |
| Require all/ip/env, Order/Allow/Deny | Yes | Yes |
| AuthType Basic + AuthUserFile | Yes | Yes |
| FilesMatch, Files, Limit/LimitExcept | Yes | Yes |
| If/ElseIf/Else conditionals | Yes | Yes |
| Redirect, RedirectMatch | Yes | Yes |
| ErrorDocument | Yes | Yes |
| ExpiresActive/ExpiresByType/ExpiresDefault | Yes | Yes |
| AddType/ForceType/AddCharset/AddDefaultCharset | Yes | Yes |
| SetEnv/SetEnvIf/BrowserMatch | Yes | Yes |
| DirectoryIndex | Yes | Yes |
| LSBruteForceProtection | Yes | Yes |
| Options -Indexes (module-level) | Yes | Yes |
| **RewriteRule execution** | Parsed only | **Full execution** |
| **php_value / php_flag** | Parsed only | **Passed to lsphp** |
| **Options -Indexes (engine-level 403)** | Module fallback | **Native 403** |
| **Apache config auto-convert** | Manual | **Automatic at startup** |

## When to Use Each Edition

### LiteHTTPD-Thin

Best for:
- Adding security headers, ACL, and caching to stock OLS
- Sites that do not need RewriteRule in `.htaccess` (WordPress rewrite handled by OLS native `RewriteFile`)
- Quick evaluation without rebuilding OLS
- Docker/container environments using official OLS images
- Hosting providers who cannot modify the OLS binary

Limitations:
- `RewriteRule [R=301,L]` in `.htaccess` will be parsed but not executed. OLS falls back to native `RewriteFile` processing, which handles basic WordPress permalinks but not advanced redirect rules.
- `php_value` and `php_flag` are parsed but have no effect on lsphp.

### LiteHTTPD-Full

Best for:
- Full Apache-to-OLS migration with 90%+ compatibility
- WordPress sites with plugins that write RewriteRule redirects (Redirection, Yoast, Rank Math)
- Sites using `php_value` for per-directory PHP configuration
- Production hosting replacing Apache or LSWS Enterprise

Includes 4 patches applied to OLS source:
- **Patch 0001** (PHPConfig): Enables `php_value`/`php_flag` via LSIAPI
- **Patch 0002** (Rewrite): Enables full `RewriteRule` execution with [R=301,L,QSA,F,G] flags
- **Patch 0003** (readApacheConf): Auto-converts Apache httpd.conf at OLS startup
- **Patch 0004** (autoIndex): Returns 403 for `Options -Indexes` at engine level

## Installation

### LiteHTTPD-Thin

```bash
# Copy module to stock OLS
cp litehttpd_htaccess.so /usr/local/lsws/modules/

# Enable module
echo 'module litehttpd_htaccess { ls_enabled 1 }' >> /usr/local/lsws/conf/httpd_config.conf

# Restart
/usr/local/lsws/bin/lswsctrl restart
```

### LiteHTTPD-Full (RPM repo -- recommended)

```bash
# Add repo and install (patched OLS + bundled module)
curl -s https://rpms.litehttpd.com/setup.sh | bash
dnf install openlitespeed-litehttpd
systemctl restart lsws
```

### LiteHTTPD-Full (from source)

See [Building from Source](/development/building/) and [Patches](/development/patches/).
