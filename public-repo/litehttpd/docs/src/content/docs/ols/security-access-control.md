---
title: "Access Control"
description: "IP-based access control for OpenLiteSpeed at server, vhost, and directory levels"
---

## Overview

OLS provides IP-based access control at three levels:

1. **Server level** -- in `httpd_config.conf`, applies to all requests
2. **Virtual host level** -- in vhost config, applies to one site
3. **Context level** -- per URL path or directory
4. **.htaccess level** -- per directory, using the LiteHTTPD module

## OLS Native Access Control

### Syntax

OLS uses `accessControl` blocks with `allow` and `deny` directives:

```apacheconf
accessControl {
  allow                   192.168.1.0/24, 10.0.0.0/8
  deny                    ALL
}
```

The format is:

- `allow` -- comma-separated list of IPs, CIDR ranges, or `ALL`
- `deny` -- comma-separated list of IPs, CIDR ranges, or `ALL`

OLS evaluates `deny` rules first, then `allow` rules. A request is denied if it matches a `deny` rule and does not match any `allow` rule.

### Server-Level Access Control

In `httpd_config.conf`:

```apacheconf
accessControl {
  allow                   ALL
  deny
}
```

### Virtual Host Level

In the virtual host configuration file:

```apacheconf
virtualhost example {
  ...
  accessControl {
    allow                 ALL
    deny                  192.168.1.100, 10.0.0.50
  }
}
```

### Context Level

Restrict access to specific URL paths:

```apacheconf
context /admin/ {
  location                $VH_ROOT/admin/
  allowBrowse             1

  accessControl {
    allow                 192.168.1.0/24
    deny                  ALL
  }
}
```

### WebAdmin Protection

Restrict the WebAdmin GUI (port 7080) to trusted IPs:

```apacheconf
listener admin {
  address                 *:7080
  secure                  1

  accessControl {
    allow                 192.168.1.0/24, 127.0.0.1
    deny                  ALL
  }
}
```

## .htaccess Access Control (LiteHTTPD)

With the LiteHTTPD module installed, you can use Apache-compatible access control directives in `.htaccess` files.

### Modern Syntax (Require)

The `Require` directive is the modern Apache 2.4+ syntax:

```apacheconf
# Allow specific IP
Require ip 192.168.1.0/24

# Allow all
Require all granted

# Deny all
Require all denied

# Multiple conditions (all must match)
<RequireAll>
  Require ip 192.168.1.0/24
  Require valid-user
</RequireAll>

# Any condition matches
<RequireAny>
  Require ip 10.0.0.0/8
  Require ip 192.168.0.0/16
</RequireAny>

# Negation
<RequireAll>
  Require all granted
  <RequireNone>
    Require ip 192.168.1.100
  </RequireNone>
</RequireAll>
```

### IPv6 Support

The LiteHTTPD module supports full IPv6 CIDR notation:

```apacheconf
Require ip 2001:db8::/32
Require ip ::1
```

### Legacy Syntax (Order/Allow/Deny)

The older Apache 2.2 syntax is also supported:

```apacheconf
# Deny by default, allow specific IPs
Order Deny,Allow
Deny from all
Allow from 192.168.1.0/24
Allow from 10.0.0.0/8

# Allow by default, deny specific IPs
Order Allow,Deny
Allow from all
Deny from 192.168.1.100
```

### Restrict by File

Protect specific files:

```apacheconf
# Block access to .env files
<Files .env>
  Require all denied
</Files>

# Block all dot-files
<FilesMatch "^\.">
  Require all denied
</FilesMatch>

# Protect wp-login.php
<Files wp-login.php>
  Require ip 192.168.1.0/24
</Files>
```

### Environment-Based Access

```apacheconf
# Allow based on environment variable
SetEnvIf User-Agent "MonitoringBot" allowed_bot
Require env allowed_bot
```

## Common Patterns

### Block Known Bad Actors

Create a deny list in the virtual host config:

```apacheconf
accessControl {
  allow                   ALL
  deny                    1.2.3.4, 5.6.7.0/24, 10.20.30.0/24
}
```

### Allow Only Internal Networks

```apacheconf
# OLS native
accessControl {
  allow                   10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 127.0.0.1
  deny                    ALL
}

# .htaccess equivalent
Require ip 10.0.0.0/8 172.16.0.0/12 192.168.0.0/16 127.0.0.1
```

### Protect Admin Areas

```apacheconf
# In .htaccess for /wp-admin/
<Files "*.php">
  <RequireAny>
    Require ip 192.168.1.0/24
    Require ip 10.0.0.0/8
  </RequireAny>
</Files>

# Allow AJAX requests from all users
<Files admin-ajax.php>
  Require all granted
</Files>
```

### Country-Level Blocking

OLS does not include built-in GeoIP. Use an external firewall (iptables, nftables, or fail2ban) or a CDN for country-level blocking.

## Troubleshooting

**403 Forbidden but IP should be allowed:**
- Check for conflicting rules at different levels (server > vhost > context)
- In `.htaccess`, verify the `Order` directive matches your intent
- Check for IPv6 vs IPv4 mismatches (client may connect via IPv6)

**Access control not taking effect from .htaccess:**
- Verify the LiteHTTPD module is loaded
- Check that `autoLoadHtaccess` is enabled in the virtual host
- Verify `.htaccess` file permissions are readable by the OLS user
