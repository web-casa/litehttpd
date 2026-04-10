---
title: Headers
description: Control HTTP response and request headers with Header and RequestHeader directives.
---

The Headers directives allow you to set, modify, and remove HTTP response headers and request headers. These are essential for security headers, CORS configuration, caching policies, and more.

## Directive Reference

| Directive | Syntax | Description |
|-----------|--------|-------------|
| `Header set` | `Header [always] set name value [env=VAR]` | Set a response header, replacing any existing value |
| `Header unset` | `Header [always] unset name` | Remove a response header |
| `Header append` | `Header [always] append name value` | Append a value to an existing header (comma-separated) |
| `Header merge` | `Header [always] merge name value` | Append a value only if it is not already present |
| `Header add` | `Header [always] add name value` | Add a header, even if one with the same name exists |
| `Header edit` | `Header [always] edit name regex replacement` | Edit a header value using a regex (first match only) |
| `Header edit*` | `Header [always] edit* name regex replacement` | Edit a header value using a regex (all matches) |
| `RequestHeader set` | `RequestHeader set name value` | Set a request header passed to the backend |
| `RequestHeader unset` | `RequestHeader unset name` | Remove a request header before it reaches the backend |

The optional `always` keyword applies the header operation on all responses, including error responses (4xx, 5xx). Without `always`, headers are only applied to successful responses.

The optional `env=VAR` condition makes the header operation apply only when the environment variable `VAR` is set.

## Examples

### Security Headers

```apache
Header always set X-Content-Type-Options "nosniff"
Header always set X-Frame-Options "SAMEORIGIN"
Header always set X-XSS-Protection "1; mode=block"
Header always set Referrer-Policy "strict-origin-when-cross-origin"
Header always set Permissions-Policy "camera=(), microphone=(), geolocation=()"
```

### CORS Configuration

```apache
Header set Access-Control-Allow-Origin "https://example.com"
Header set Access-Control-Allow-Methods "GET, POST, OPTIONS"
Header set Access-Control-Allow-Headers "Content-Type, Authorization"
```

### Conditional Headers with env

```apache
SetEnvIf Request_URI "\.pdf$" is_pdf
Header set Content-Disposition "attachment" env=is_pdf
```

### Edit Headers with Regex

```apache
# Remove the Server version from the header value
Header edit Server "Apache/.*" "Apache"

# Replace all occurrences of http with https in Location headers
Header edit* Location "http://" "https://"
```

### Remove Unwanted Headers

```apache
Header unset X-Powered-By
Header always unset Server
RequestHeader unset Proxy
```

:::note
In OLS, `RequestHeader set` writes to the `HTTP_*` environment variables consumed by the backend (e.g., lsphp). It does not modify the raw request stream the way Apache does.
:::
