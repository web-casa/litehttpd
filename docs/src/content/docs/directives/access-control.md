---
title: Access Control
description: Control access to resources by IP address, environment variable, or global policy.
---

Access control directives restrict who can access your site or specific directories. LiteHTTPD supports both the legacy `Order`/`Allow`/`Deny` syntax and the modern `Require` syntax.

## Directive Reference

| Directive | Syntax | Description |
|-----------|--------|-------------|
| `Order` | `Order Allow,Deny` or `Order Deny,Allow` | Set the evaluation order for Allow/Deny rules |
| `Allow from` | `Allow from ip\|cidr\|all` | Allow access from specified addresses |
| `Deny from` | `Deny from ip\|cidr\|all` | Deny access from specified addresses |
| `Require all granted` | `Require all granted` | Allow access to everyone (modern syntax) |
| `Require all denied` | `Require all denied` | Deny access to everyone (modern syntax) |
| `Require ip` | `Require ip ip\|cidr [...]` | Allow access from specified IPs or CIDR ranges |
| `Require not ip` | `Require not ip ip\|cidr [...]` | Deny access from specified IPs or CIDR ranges |
| `Require env` | `Require env VAR` | Allow access when the named environment variable is set |

## Examples

### Block Access to a Directory (Legacy Syntax)

```apache
Order Deny,Allow
Deny from all
Allow from 192.168.1.0/24
Allow from 10.0.0.0/8
```

### Block Access (Modern Syntax)

```apache
Require all denied
```

### Allow Only Specific IPs

```apache
Require ip 203.0.113.50
Require ip 2001:db8::/32
```

### Protect WordPress Admin

```apache
<Files "wp-login.php">
  Order Deny,Allow
  Deny from all
  Allow from 203.0.113.50
</Files>
```

### Block PHP Execution in Uploads

```apache
# Place in wp-content/uploads/.htaccess
<FilesMatch "\.php$">
  Require all denied
</FilesMatch>
```

### Conditional Access with Environment

```apache
SetEnvIf X-Forwarded-For "^10\." is_internal
Require env is_internal
```

:::note
Both IPv4 and IPv6 addresses are supported in all IP-based directives, including CIDR notation (e.g., `192.168.0.0/16`, `2001:db8::/32`).
:::

:::caution
When using `Order Allow,Deny`, the default policy is to deny. When using `Order Deny,Allow`, the default policy is to allow. Make sure you choose the correct order for your use case.
:::
