---
title: Compatibility Results
description: .htaccess compatibility test results across 4 engines
---

## WordPress + 19 Plugins Test

| Test | Apache | LiteHTTPD | Stock OLS | LSWS |
|------|--------|-----------|-----------|------|
| Homepage (200) | 200 | 200 | 404 | 200 |
| wp-login.php | 200 | 200 | 200 | 200 |
| wp-admin redirect | 302 | 302 | 404 | 302 |
| .htaccess protection | 403 | 403 | 403 | 403 |
| .htpasswd protection | 403 | 403 | 404 | 403 |
| Options -Indexes | 403 | 403 | 404 | 403 |
| Require all denied | 403 | 403 | 200 | 403 |
| FilesMatch deny .bak | 403 | 403 | 200 | 403 |
| RewriteRule [R=301] | 301 | 301 | 404 | 301 |
| Header set | Yes | Yes | No | Yes |
| Cache-Control | Yes | Yes | No | Yes |
| AddCharset | Yes | Yes | No | Yes |

:::note
"LiteHTTPD" in the tables above refers to the Full edition with all OLS patches applied. LiteHTTPD-Thin achieves the same results for ACL, Header, and Expires tests but RewriteRule tests require the Full edition.
:::

## Compatibility Rate

| Engine | Match | Rate |
|--------|-------|------|
| **LiteHTTPD** | **18/18** | **90%+** |
| LSWS Enterprise 6.3.5 | 18/18 | 100% |
| Stock OLS 1.8.5 | 8/18 | 44% |
