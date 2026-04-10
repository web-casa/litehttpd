---
title: Configuration
description: How to configure LiteHTTPD module settings
---

## Module Configuration (all editions)

Add to `/usr/local/lsws/conf/httpd_config.conf`:

```
module litehttpd_htaccess {
    ls_enabled              1
}
```

Add to your vhost config (`/usr/local/lsws/conf/vhosts/<name>/vhconf.conf`):

```
allowOverride 255
autoLoadHtaccess 1
```

### AllowOverride Values

| Value | Category | Directives |
|-------|----------|-----------|
| `1` | Limit | Order, Allow, Deny |
| `2` | Auth | AuthType, AuthName, Require, Satisfy |
| `4` | FileInfo | AddType, Redirect, Rewrite, ErrorDocument |
| `8` | Indexes | Options Indexes |
| `16` | Options | Options (non-Indexes), Expires, Limit |
| `255` | All | All directives (recommended) |

## Full Edition Configuration

### RewriteRule execution

RewriteRule execution is enabled automatically when a patched OLS binary is detected. No additional configuration is needed.

### php_value / php_flag

With the patched OLS binary, `php_value` and `php_flag` directives in `.htaccess` are passed to lsphp via the LSIAPI PHPConfig API.

### Apache config auto-conversion (Patch 0003)

The `readApacheConf` directive converts an existing Apache httpd.conf to OLS native config at startup:

```
readApacheConf /etc/httpd/conf/httpd.conf portmap=80:8088
```

## PHP Performance Tuning (all editions)

For production deployments, configure lsphp worker pool:

```
extProcessor lsphp {
    env                     PHP_LSAPI_CHILDREN=10
    env                     PHP_LSAPI_MAX_IDLE=300
}
```

This pre-starts 10 PHP workers, eliminating cold-start latency.

## Thin Edition Limitations

When running LiteHTTPD-Thin (module only, stock OLS binary):

- `RewriteRule` directives are parsed but not executed -- requests fall through to OLS native routing
- `php_value` and `php_flag` are parsed but not passed to lsphp
- `Options -Indexes` is stored as a hint but not enforced at engine level
- `readApacheConf` is not available
