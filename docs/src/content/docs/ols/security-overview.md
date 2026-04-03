---
title: "Security Overview"
description: "Overview of OpenLiteSpeed security features and hardening options"
---

## Security Layers

OpenLiteSpeed provides security at multiple levels:

1. **Built-in protections** -- connection limits, per-client throttling, anti-DDoS
2. **ModSecurity** -- web application firewall with OWASP CRS
3. **Access control** -- IP-based restrictions at server, vhost, and context levels
4. **reCAPTCHA** -- server-level bot mitigation
5. **Security headers** -- HTTP response headers for browser-side protection
6. **LiteHTTPD module** -- `.htaccess`-based security directives (Require, Order/Allow/Deny, Header)

## Built-in Protections

OLS includes several anti-abuse mechanisms configured in `httpd_config.conf`:

### Per-Client Throttling

```apacheconf
security {
  perClientConnLimit {
    staticReqsPerSec      40
    dynReqsPerSec         4
    outBandwidth          0
    inBandwidth           0
    softLimit             500
    hardLimit             1000
    blockBadReq           1
    gracePeriod           15
    banPeriod             60
  }
}
```

| Setting | Description |
|---------|-------------|
| `staticReqsPerSec` | Max static file requests per second per IP |
| `dynReqsPerSec` | Max dynamic (PHP) requests per second per IP |
| `softLimit` | Connections at which throttling begins |
| `hardLimit` | Connections at which new connections are refused |
| `blockBadReq` | Block malformed HTTP requests (set to `1`) |
| `gracePeriod` | Seconds before ban takes effect |
| `banPeriod` | Seconds an IP is banned after exceeding limits |

### Connection Limits

```apacheconf
tuning {
  maxConnections          10000
  maxSSLConnections       10000
  connTimeout             300
  maxKeepAliveReq         10000
  keepAliveTimeout        5
}
```

### CGI Security

OLS does not enable CGI by default. If needed, restrict it carefully:

```apacheconf
security {
  CGIRLimit {
    maxCGIInstances       20
    minUID                11
    minGID                10
    forceGID              0
  }
}
```

## Access Control

OLS provides native IP-based access control at three levels:

- **Server level** -- applies to all requests
- **Virtual host level** -- applies to a specific vhost
- **Context level** -- applies to a specific URL path

See [Access Control](/ols/security-access-control/) for detailed configuration.

## ModSecurity (WAF)

OLS includes built-in ModSecurity v3 support for web application firewall protection. Combined with the OWASP Core Rule Set, it defends against SQL injection, XSS, and other common attacks.

See [ModSecurity](/ols/security-modsecurity/) for setup instructions.

## reCAPTCHA Protection

OLS can present a CAPTCHA challenge to suspicious clients before allowing access, providing server-level DDoS mitigation without application changes.

See [reCAPTCHA](/ols/security-recaptcha/) for configuration.

## Security Headers

HTTP security headers instruct browsers to enable protections like HSTS, CSP, and frame embedding restrictions. Configure them natively in OLS or via `.htaccess` with the LiteHTTPD module.

See [Security Headers](/ols/security-headers/) for examples.

## LiteHTTPD Module Security Features

When the LiteHTTPD module (`ols_htaccess.so`) is installed, you gain `.htaccess`-based security controls that are familiar to Apache administrators:

### Authentication and Authorization

```apacheconf
# .htaccess
<RequireAll>
  Require ip 192.168.1.0/24
  Require valid-user
</RequireAll>
```

### Legacy Access Control

```apacheconf
# .htaccess
Order Deny,Allow
Deny from all
Allow from 192.168.1.0/24
```

### Brute Force Protection

Block PHP execution in upload directories:

```apacheconf
# wp-content/uploads/.htaccess
<FilesMatch "\.php$">
  Require all denied
</FilesMatch>
```

### Security Headers via .htaccess

```apacheconf
Header set X-Frame-Options "SAMEORIGIN"
Header set X-Content-Type-Options "nosniff"
Header set Strict-Transport-Security "max-age=31536000; includeSubDomains"
```

## Recommended Security Checklist

- [ ] Change WebAdmin default password (port 7080)
- [ ] Restrict WebAdmin to trusted IPs
- [ ] Enable per-client throttling
- [ ] Install and configure ModSecurity with OWASP CRS
- [ ] Set security response headers (HSTS, CSP, X-Frame-Options)
- [ ] Disable directory listing (`autoIndex` set to `0`)
- [ ] Block access to sensitive files (`.env`, `.git`, `.htaccess`)
- [ ] Use TLS 1.2+ and disable weak ciphers (see [SSL/TLS](/ols/ssl/))
- [ ] Set `blockBadReq` to `1` in per-client throttle config
- [ ] Restrict PHP execution in upload/static directories
