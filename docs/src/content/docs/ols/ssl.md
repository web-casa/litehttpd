---
title: "SSL / TLS"
description: "Configure SSL/TLS certificates, Let's Encrypt integration, and security settings for OpenLiteSpeed."
---

## Listener SSL Configuration

Enable SSL on a listener in `/usr/local/lsws/conf/httpd_config.conf`:

```apacheconf
listener HTTPS {
    address                 *:443
    secure                  1
    keyFile                 /etc/letsencrypt/live/example.com/privkey.pem
    certFile                /etc/letsencrypt/live/example.com/fullchain.pem
    certChain               1
    sslProtocol             24
    ciphers                 ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384
    enableECDHE             1
    enableDHE               0
    map                     example.com example.com, www.example.com
}
```

### SSL Parameters

| Parameter | Description |
|---|---|
| `keyFile` | Path to the PEM-encoded private key |
| `certFile` | Path to the PEM-encoded certificate (use fullchain for best compatibility) |
| `certChain` | Set to `1` to enable certificate chain verification |
| `sslProtocol` | Bitmask for enabled protocols. `24` = TLS 1.2 + TLS 1.3 |
| `ciphers` | Colon-separated cipher suite list |
| `enableECDHE` | Enable ECDHE key exchange (`1` = on) |
| `enableDHE` | Enable DHE key exchange (`0` = off, recommended for performance) |

### Protocol Bitmask Values

| Value | Protocol |
|---|---|
| 1 | SSL 3.0 (insecure, do not use) |
| 2 | TLS 1.0 (deprecated) |
| 4 | TLS 1.1 (deprecated) |
| 8 | TLS 1.2 |
| 16 | TLS 1.3 |
| 24 | TLS 1.2 + TLS 1.3 (recommended) |

## Let's Encrypt Integration

### Using Certbot

Install certbot and obtain certificates:

```bash
# AlmaLinux / Rocky
dnf install certbot

# Ubuntu / Debian
apt install certbot
```

Obtain a certificate using the webroot method. OLS serves the challenge files automatically:

```bash
certbot certonly --webroot -w /var/www/example.com/public/ -d example.com -d www.example.com
```

Then point the listener to the generated files:

```apacheconf
keyFile                 /etc/letsencrypt/live/example.com/privkey.pem
certFile                /etc/letsencrypt/live/example.com/fullchain.pem
```

Restart OLS:

```bash
systemctl restart lsws
```

### Auto-Renewal

Certbot installs a systemd timer or cron job for renewal. Add a post-renewal hook to restart OLS:

```bash
# Create /etc/letsencrypt/renewal-hooks/deploy/restart-ols.sh
#!/bin/bash
systemctl restart lsws
```

```bash
chmod +x /etc/letsencrypt/renewal-hooks/deploy/restart-ols.sh
```

Test renewal:

```bash
certbot renew --dry-run
```

## Per-Virtual-Host SSL

For SNI-based multi-domain SSL, configure certificates per virtual host in the vhost config or via WebAdmin. OLS automatically selects the correct certificate based on the requested hostname.

In the WebAdmin GUI:
1. Go to **Virtual Hosts** > your vhost > **SSL**.
2. Set **Private Key File** and **Certificate File**.
3. Save and restart.

## HTTP to HTTPS Redirect

Add a rewrite rule to redirect all HTTP traffic to HTTPS. In the virtual host config:

```apacheconf
rewrite {
    enable                  1
    rules                   <<<END_rules
RewriteCond %{HTTPS} !on
RewriteRule ^(.*)$ https://%{HTTP_HOST}%{REQUEST_URI} [R=301,L]
    END_rules
}
```

Or in `.htaccess` (with the LiteHTTPD module):

```apacheconf
RewriteEngine On
RewriteCond %{HTTPS} !on
RewriteRule ^(.*)$ https://%{HTTP_HOST}%{REQUEST_URI} [R=301,L]
```

## OCSP Stapling

Enable OCSP stapling for faster SSL handshakes. In the WebAdmin GUI:

1. Go to **Listeners** > your HTTPS listener > **SSL** tab.
2. Set **Enable OCSP Stapling** to `Yes`.
3. Save and restart.

## Security Recommendations

- Use `sslProtocol 24` (TLS 1.2 + 1.3 only).
- Disable DHE in favor of ECDHE for better performance.
- Use a strong cipher list. Prefer GCM and ChaCha20 ciphers.
- Enable HSTS headers. See [Security Headers](/ols/security-headers/).
- Regularly renew certificates before expiration.

## Next Steps

- [Listeners](/ols/listeners/) -- listener configuration basics.
- [Security Headers](/ols/security-headers/) -- add HSTS and other headers.
- [Virtual Hosts](/ols/virtual-hosts/) -- per-vhost SSL settings.
