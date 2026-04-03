# .htaccess Compatibility Test — Apache vs LiteHTTPD vs Stock OLS vs LSWS Enterprise

## Test Environment

| Item | Value |
|------|-------|
| VPS | 4x Linode g6-standard-4 (4 vCPU, 8 GB RAM) |
| OS | AlmaLinux 9.5 |
| WordPress | 6.9.4 + 19 plugins |
| .htaccess | ~217 lines (15 plugins auto-generated) |
| Date | 2026-04-03 |

## Results

| Category | Test | Apache 2.4 | LiteHTTPD | Stock OLS | LSWS 6.3.5 |
|----------|------|------------|-----------|-----------|------------|
| Baseline | Homepage | 200 | 200 ✅ | 404 ❌ | 200 ✅ |
| | wp-login.php | 200 | 200 ✅ | 200 ✅ | 200 ✅ |
| | wp-admin redirect | 302 | 302 ✅ | 404 ❌ | 302 ✅ |
| | wp-cron.php | 200 | 200 ✅ | 200 ✅ | 200 ✅ |
| | xmlrpc.php | 405 | 405 ✅ | 405 ✅ | 405 ✅ |
| | 404 page | 404 | 404 ✅ | 404 ✅ | 404 ✅ |
| | Static CSS | 200 | 200 ✅ | 200 ✅ | 200 ✅ |
| Security | .htaccess protection | 403 | 403 ✅ | 403 ✅ | 403 ✅ |
| | .htpasswd protection | 403 | 403 ✅ | 404 ❌ | 403 ✅ |
| | Options -Indexes | 403 | 403 ✅ | 404 ❌ | 403 ✅ |
| | Require all denied | 403 | 403 ✅ | 200 ❌ | 403 ✅ |
| | FilesMatch deny .bak | 403 | 403 ✅ | 200 ❌ | 403 ✅ |
| | FilesMatch allow .html | 200 | 200 ✅ | 200 ✅ | 200 ✅ |
| Rewrite | RewriteRule [R=301] | 301 | 301 ✅ | 404 ❌ | 301 ✅ |
| Headers | Header set X-Custom | ✓ | ✓ ✅ | ✗ ❌ | ✓ ✅ |
| | ExpiresByType CSS | ✓ | ✓ ✅ | ✓ ✅ | ✓ ✅ |
| | Cache-Control JS | max-age=31536000 | max-age=31536000 ✅ | (none) ❌ | public, max-age=31536000 ✅ |
| | AddCharset .html | text/html; charset=utf-8 | text/html; charset=utf-8 ✅ | text/html ❌ | text/html ✅ |

## Compatibility Rate

| Engine | Match | Rate |
|--------|-------|------|
| **LiteHTTPD** | **18/18** | **90%+** |
| **LSWS Enterprise 6.3.5** | **18/18** | **100%** |
| Stock OLS 1.8.5 | 8/18 | 44% |
