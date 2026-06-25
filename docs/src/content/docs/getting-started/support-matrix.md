---
title: Support Matrix
description: Supported platforms, editions, package types, and feature coverage
---

Use this page to choose the right LiteHTTPD edition and installation path before touching a production server.

## Platform Support

| Environment | CPU | LiteHTTPD-Full | LiteHTTPD-Thin | Notes |
|-------------|-----|----------------|----------------|-------|
| EL 8/9/10 compatible distros | x86_64 | Supported via `openlitespeed-litehttpd` RPM | Supported | The RPM repository setup script currently accepts x86_64 only. |
| Ubuntu 22.04+ / Debian 12+ | x86_64 | Build from source or use a matching release asset | Supported via module install | The RPM repo is for EL-style systems. |
| Official OpenLiteSpeed Docker images | x86_64 | Requires a custom image with the patched OLS binary | Supported | Thin is the least invasive path for existing images. |
| CyberPanel / aaPanel on EL systems | x86_64 | Supported after backup and validation | Supported | Control panels may regenerate vhost files; keep `autoLoadHtaccess 0`. |
| ARM64 / aarch64 | ARM64 | Source build only | Source/module build only | Pre-built RPM repository packages are not currently published for ARM64. |

## Edition Feature Support

| Feature | LiteHTTPD-Thin | LiteHTTPD-Full |
|---------|----------------|----------------|
| Parse 83 Apache `.htaccess` directive types | Yes | Yes |
| Header, ACL, authentication, containers, conditionals | Yes | Yes |
| Redirect and RedirectMatch | Yes | Yes |
| BruteForceProtection directives | Yes | Yes |
| RewriteRule execution from `.htaccess` | Parsed only | Full execution |
| `php_value` / `php_flag` passthrough to lsphp | Parsed only | Supported |
| `Options -Indexes` engine-level 403 | Module fallback | Native 403 |
| `readApacheConf` startup conversion | Not available | Supported |
| Apache-style per-directory handler remapping | Parsed only | Parsed only |

## Required OLS Settings

Every LiteHTTPD deployment needs the module enabled:

```apacheconf
module litehttpd_htaccess {
    ls_enabled              1
}
```

Every vhost that should read `.htaccess` should use:

```apacheconf
allowOverride 255
autoLoadHtaccess 0
```

LiteHTTPD reads `.htaccess` through its own module hooks. Leave OLS native `autoLoadHtaccess` disabled to avoid double-processing directives such as `ErrorDocument` and `Options`.

## Known Boundaries

- `AddHandler`, `SetHandler`, `RemoveHandler`, and `Action` are parsed for compatibility, but request handling still comes from OLS `scriptHandler` and `extProcessor` configuration.
- Thin mode is suitable for security headers, ACLs, authentication, redirects, and quick evaluation. Use Full mode when your site depends on `.htaccess` `RewriteRule` execution or `php_value`.
- The RPM repository is the recommended path for EL 8/9/10 x86_64 production installs. Use source builds for other platforms.
