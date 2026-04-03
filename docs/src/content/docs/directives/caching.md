---
title: Caching
description: Expires and cache control directives
---

## Directives

| Directive | Syntax |
|-----------|--------|
| `ExpiresActive` | `ExpiresActive On\|Off` |
| `ExpiresByType` | `ExpiresByType MIME-type "base plus num unit"` |
| `ExpiresDefault` | `ExpiresDefault "base plus num unit"` |

## Time Units

`years`, `months`, `weeks`, `days`, `hours`, `minutes`, `seconds`

## Base Values

| Base | Meaning |
|------|---------|
| `access` | Time of client access (request time) |
| `modification` | File modification time (treated as access in OLS) |

## Examples

### Standard Cache Headers

```apache
ExpiresActive On
ExpiresByType text/css "access plus 1 year"
ExpiresByType application/javascript "access plus 1 year"
ExpiresByType image/jpeg "access plus 1 month"
ExpiresByType image/png "access plus 1 month"
ExpiresByType image/webp "access plus 1 month"
ExpiresByType font/woff2 "access plus 1 year"
ExpiresDefault "access plus 2 days"
```

This generates `Cache-Control: max-age=N` and `Expires:` response headers automatically.

### W3 Total Cache Style

```apache
ExpiresActive On
ExpiresByType text/css "access plus 31536000 seconds"
ExpiresByType application/x-javascript "access plus 31536000 seconds"
ExpiresByType image/gif "access plus 31536000 seconds"
```
