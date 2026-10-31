---
title: Known Differences
description: Behavioral differences between LiteHTTPD and Apache httpd, and between LiteHTTPD and stock OLS
---

## LiteHTTPD vs Stock OLS

These are behavioral changes introduced when you add LiteHTTPD to a stock OLS installation.

### .ht* File Protection

| | Stock OLS | LiteHTTPD |
|-|-----------|-----------|
| Request to `.htaccess` | Serves file (200) or file not found (404) | **403 Forbidden** |
| Request to `.htpasswd` | Same | **403 Forbidden** |

LiteHTTPD enforces Apache's default `<Files ".ht*"> Require all denied</Files>` behavior. Stock OLS has no such protection — `.htaccess` files may be publicly readable.

### Path Traversal

| | Stock OLS | LiteHTTPD |
|-|-----------|-----------|
| Encoded `../` (e.g., `%2e%2e/`) | 400 or 404 (engine-dependent) | **403 Forbidden** |

LiteHTTPD adds defense-in-depth path traversal detection for percent-encoded bypass attempts.

### Previously Ignored .htaccess Directives

Stock OLS ignores most `.htaccess` directives. After adding LiteHTTPD, all 80 supported directives become active. If your existing `.htaccess` files contain rules that were previously inert, they now take effect:

- `Require all denied` → blocks access (403)
- `Header set` → adds response headers
- `FilesMatch` → enforces access control
- `AuthType Basic` → requires authentication (401)

**Review your `.htaccess` files before deploying LiteHTTPD in production.**

### Options -Indexes

| | Stock OLS | LiteHTTPD (Full) | LiteHTTPD (Thin) |
|-|-----------|-----------------|-----------------|
| Directory without index file | 404 | **403** (with patch 0004) | 404 (unchanged) |

Stock OLS returns 404 for directories without an index file. With LiteHTTPD Full (patch 0004), `Options -Indexes` returns 403, matching Apache behavior.

### ExecCGI Blocked

`Options +ExecCGI` in `.htaccess` is silently ignored by LiteHTTPD, regardless of edition. Apache allows this if `AllowOverride Options` is set. This is a deliberate security restriction.

---

## LiteHTTPD vs Apache httpd

### Fully Compatible (No Differences)

The following features work identically to Apache:
- All 80 supported directives
- Directive merging across directory levels
- AllowOverride category filtering
- If/ElseIf/Else conditional expression evaluation
- FilesMatch regex matching
- RequireAny/RequireAll authorization logic

### Header Name Case

| Apache | LiteHTTPD |
|--------|-----------|
| `X-Custom-Header: value` | `x-custom-header: value` |

OLS lowercases response header names. This is valid per HTTP/1.1 (RFC 7230) and required by HTTP/2. No functional impact.

### Handler Directives (No-op)

`AddHandler`, `SetHandler`, `RemoveHandler`, and `Action` are parsed but do not change request handling. OLS uses `scriptHandler` in vhost config for handler mapping.

### RewriteRule inside If blocks

```apache
# Not supported
<If "%{REQUEST_URI} =~ /^\/api\//">
  RewriteRule ^api/(.*)$ /handler.php?path=$1 [L]
</If>
```

Rewrite directives inside `<If>` blocks are logged and skipped. Place rewrite rules at the top level of `.htaccess`.

### Consecutive Header append

Multiple `Header append` directives for the same header name may only retain the last value in some OLS configurations. Use `Header set` with the complete value instead.

### ErrorDocument with Local File Path (5xx Errors)

For `ErrorDocument 404`, LiteHTTPD handles local file paths correctly (the most common use case). However, for 5xx errors from PHP/backend, `ErrorDocument 500 /error.html` cannot fully replace the response body because OLS commits `Content-Length` before the module hook fires. Use an external URL redirect instead:

```apache
# Works for all status codes:
ErrorDocument 500 https://example.com/error.html

# Works for 404 (pre-checked at URI_MAP):
ErrorDocument 404 /error.html

# May not replace body for 5xx (use URL redirect instead):
ErrorDocument 500 /error.html
```

### FollowSymLinks / MultiViews

`Options FollowSymLinks` and `Options MultiViews` are passed to OLS's engine, but OLS may not honor them identically to Apache. Test these if your site relies on them.

---

## OLS-Specific Behavior

### Directory Listing

Stock OLS returns 404 for directories without an index file (not 200 with listing like Apache). With Patch 0004 and `Options -Indexes`, LiteHTTPD returns 403 matching Apache behavior.

### PHP SAPI

Apache typically uses PHP-FPM (FastCGI), while OLS uses lsphp (LSAPI). The LSAPI protocol is more efficient but has different process management. See [PHP Tuning](/performance/php-tuning/) for configuration.

### autoLoadHtaccess Double-Processing

If OLS's native `autoLoadHtaccess` is enabled alongside LiteHTTPD, directives that both systems understand (`ErrorDocument`, `Options`) may be processed twice. Disable `autoLoadHtaccess` in your vhost config when using LiteHTTPD:

```
autoLoadHtaccess 0
```

---

## Thin Edition Additional Limitations

LiteHTTPD-Thin (the `.so` module running on stock OLS without patches) has the following additional limitations compared to the Full edition:

| Feature | Full Edition | Thin Edition |
|---------|-------------|--------------|
| RewriteRule execution | Executed by patched OLS engine | Parsed but not executed; falls back to OLS native `RewriteFile` processing |
| php_value / php_flag | Passed to lsphp via LSIAPI | Parsed but cannot be passed to lsphp (no PHPConfig patch) |
| Options -Indexes (403) | Returns 403 (with Patch 0004) | Returns 404 (stock OLS behavior) |
| readApacheConf | Auto-converts Apache config at startup | Not available |

All other directives (Header, Require, FilesMatch, Auth, Expires, SetEnv, If/ElseIf/Else, etc.) work identically in both editions.
