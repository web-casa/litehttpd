---
title: Known Differences
description: Behavioral differences between LiteHTTPD and Apache httpd
---

## Fully Compatible (No Differences)

The following features work identically to Apache:
- All 80 supported directives
- Directive merging across directory levels
- AllowOverride category filtering
- If/ElseIf/Else conditional expression evaluation
- FilesMatch regex matching
- RequireAny/RequireAll authorization logic

## Minor Differences

### Header Name Case

| Apache | LiteHTTPD |
|--------|-----------|
| `X-Custom-Header: value` | `x-custom-header: value` |

OLS lowercases response header names. This is valid per HTTP/1.1 (RFC 7230) and required by HTTP/2. No functional impact.

### Handler Directives (No-op)

`AddHandler`, `SetHandler`, `RemoveHandler`, and `Action` are parsed but do not change request handling. OLS uses `scriptHandler` in vhost config instead.

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

## OLS-Specific Behavior

### Directory Listing

Stock OLS returns 404 for directories without an index file (not 200 with listing like Apache). With Patch 0004 and `Options -Indexes`, LiteHTTPD returns 403 matching Apache behavior.

### PHP SAPI

Apache uses PHP-FPM (FastCGI), while OLS uses lsphp (LSAPI). The LSAPI protocol is more efficient but has different process management. See [PHP Tuning](/performance/php-tuning/) for configuration.

## Thin Edition Additional Limitations

LiteHTTPD-Thin (the `.so` module running on stock OLS without patches) has the following additional limitations compared to the Full edition:

| Feature | Full Edition | Thin Edition |
|---------|-------------|--------------|
| RewriteRule execution | Executed by patched OLS engine | Parsed but not executed; falls back to OLS native `RewriteFile` processing |
| php_value / php_flag | Passed to lsphp via LSIAPI | Parsed but cannot be passed to lsphp (no PHPConfig patch) |
| Options -Indexes (403) | Returns 403 (with Patch 0004) | Returns 404 (stock OLS behavior) |
| readApacheConf | Auto-converts Apache config at startup | Not available |

All other directives (Header, Require, FilesMatch, Auth, Expires, SetEnv, If/ElseIf/Else, etc.) work identically in both editions.
