---
title: "Security Headers"
description: "Configuring security response headers on OpenLiteSpeed"
---

## Overview

Security headers instruct browsers to enable built-in protections against common attacks like clickjacking, XSS, MIME sniffing, and protocol downgrade. OLS supports setting headers natively or via `.htaccess` with the LiteHTTPD module.

## Essential Security Headers

| Header | Purpose |
|--------|---------|
| `Strict-Transport-Security` | Force HTTPS connections (HSTS) |
| `X-Frame-Options` | Prevent clickjacking |
| `X-Content-Type-Options` | Prevent MIME type sniffing |
| `Content-Security-Policy` | Control resource loading |
| `Referrer-Policy` | Control referrer information |
| `Permissions-Policy` | Restrict browser features |
| `X-XSS-Protection` | Legacy XSS filter (deprecated but still useful) |

## OLS Native Configuration

### Server-Level Headers

Apply headers to all virtual hosts in `httpd_config.conf`:

```apacheconf
extraHeaders {
  set Strict-Transport-Security "max-age=31536000; includeSubDomains; preload"
  set X-Frame-Options "SAMEORIGIN"
  set X-Content-Type-Options "nosniff"
  set Referrer-Policy "strict-origin-when-cross-origin"
  set Permissions-Policy "camera=(), microphone=(), geolocation=()"
  set X-XSS-Protection "1; mode=block"
}
```

### Per-VHost Headers

In the virtual host configuration:

```apacheconf
virtualhost example {
  ...
  extraHeaders {
    set Strict-Transport-Security "max-age=31536000; includeSubDomains"
    set X-Frame-Options "DENY"
    set Content-Security-Policy "default-src 'self'; script-src 'self' 'unsafe-inline'"
  }
}
```

### Per-Context Headers

```apacheconf
context /api/ {
  ...
  extraHeaders {
    set Access-Control-Allow-Origin "https://example.com"
    set Access-Control-Allow-Methods "GET, POST, OPTIONS"
    set Access-Control-Allow-Headers "Content-Type, Authorization"
  }
}
```

## .htaccess Headers (LiteHTTPD)

With the LiteHTTPD module, use standard Apache `Header` directives in `.htaccess`:

```apacheconf
# Set headers
Header set X-Frame-Options "SAMEORIGIN"
Header set X-Content-Type-Options "nosniff"
Header set Strict-Transport-Security "max-age=31536000; includeSubDomains"
Header set Referrer-Policy "strict-origin-when-cross-origin"
Header set Permissions-Policy "camera=(), microphone=(), geolocation=()"

# Append to existing header
Header append X-Frame-Options "SAMEORIGIN"

# Remove a header
Header unset X-Powered-By
Header unset Server

# Conditional header
Header set X-Robots-Tag "noindex, nofollow" env=staging
```

## Header Configuration Examples

### HSTS (HTTP Strict Transport Security)

Forces browsers to use HTTPS for all future requests to your domain:

```apacheconf
Header set Strict-Transport-Security "max-age=31536000; includeSubDomains; preload"
```

- `max-age=31536000` -- remember for 1 year
- `includeSubDomains` -- apply to all subdomains
- `preload` -- opt in to browser preload lists (submit at hstspreload.org)

:::caution
Only add `preload` if you are certain all subdomains support HTTPS. Removal from the preload list can take months.
:::

### Content Security Policy (CSP)

Controls which resources the browser is allowed to load:

```apacheconf
# Basic restrictive policy
Header set Content-Security-Policy "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; font-src 'self'; connect-src 'self'; frame-ancestors 'none'"

# WordPress-friendly policy
Header set Content-Security-Policy "default-src 'self'; script-src 'self' 'unsafe-inline' 'unsafe-eval'; style-src 'self' 'unsafe-inline'; img-src 'self' data: https:; font-src 'self' data:; connect-src 'self'"
```

Start with `Content-Security-Policy-Report-Only` to test without blocking:

```apacheconf
Header set Content-Security-Policy-Report-Only "default-src 'self'; report-uri /csp-report"
```

### X-Frame-Options

Prevents your site from being embedded in iframes (clickjacking protection):

```apacheconf
# Block all framing
Header set X-Frame-Options "DENY"

# Allow same-origin framing only
Header set X-Frame-Options "SAMEORIGIN"
```

### X-Content-Type-Options

Prevents browsers from MIME-sniffing a response away from the declared content type:

```apacheconf
Header set X-Content-Type-Options "nosniff"
```

### Referrer-Policy

Controls how much referrer information is sent with requests:

```apacheconf
# Send origin only on cross-origin requests
Header set Referrer-Policy "strict-origin-when-cross-origin"

# Never send referrer
Header set Referrer-Policy "no-referrer"
```

### Permissions-Policy

Restricts browser features like camera, microphone, and geolocation:

```apacheconf
Header set Permissions-Policy "camera=(), microphone=(), geolocation=(), payment=()"
```

### Remove Information Disclosure Headers

```apacheconf
Header unset X-Powered-By
Header unset Server
```

## Complete Example

### OLS Native (httpd_config.conf)

```apacheconf
extraHeaders {
  set Strict-Transport-Security "max-age=31536000; includeSubDomains"
  set X-Frame-Options "SAMEORIGIN"
  set X-Content-Type-Options "nosniff"
  set Referrer-Policy "strict-origin-when-cross-origin"
  set Permissions-Policy "camera=(), microphone=(), geolocation=()"
  unset X-Powered-By
}
```

### .htaccess (LiteHTTPD)

```apacheconf
Header set Strict-Transport-Security "max-age=31536000; includeSubDomains"
Header set X-Frame-Options "SAMEORIGIN"
Header set X-Content-Type-Options "nosniff"
Header set Referrer-Policy "strict-origin-when-cross-origin"
Header set Permissions-Policy "camera=(), microphone=(), geolocation=()"
Header set Content-Security-Policy "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data: https:"
Header unset X-Powered-By
```

## Verify Headers

```bash
curl -I https://example.com
```

Or use online tools like [securityheaders.com](https://securityheaders.com) to scan your site and get a grade.

## Troubleshooting

**Headers not appearing in response:**
- For `.htaccess`: verify the LiteHTTPD module is loaded and `autoLoadHtaccess` is enabled
- For OLS native: check `extraHeaders` is in the correct block (server, vhost, or context)
- Restart OLS after configuration changes

**CSP blocking legitimate resources:**
- Use `Content-Security-Policy-Report-Only` first
- Check the browser console for CSP violation messages
- Add the blocked domain to the appropriate directive
