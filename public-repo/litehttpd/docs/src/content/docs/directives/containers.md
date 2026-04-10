---
title: Containers
description: File matching and authorization container directives
---

## Directives

| Directive | Syntax |
|-----------|--------|
| `<Files>` | `<Files "filename">` |
| `<FilesMatch>` | `<FilesMatch "regex">` |
| `<Limit>` | `<Limit METHOD [METHOD ...]>` |
| `<LimitExcept>` | `<LimitExcept METHOD [METHOD ...]>` |
| `<IfModule>` | `<IfModule [!]module_name>` |
| `<RequireAny>` | `<RequireAny>` |
| `<RequireAll>` | `<RequireAll>` |

## Examples

### Protect Sensitive Files

```apache
<Files "wp-config.php">
  Require all denied
</Files>

<FilesMatch "\.(bak|sql|log|env|git)$">
  Require all denied
</FilesMatch>
```

### HTTP Method Restriction

```apache
<Limit POST PUT DELETE>
  Require all denied
</Limit>
```

### Conditional Module Loading

```apache
<IfModule mod_headers.c>
  Header set X-Frame-Options "SAMEORIGIN"
</IfModule>

<IfModule !mod_nonexistent.c>
  Header set X-Fallback "active"
</IfModule>
```

### Authorization Logic

```apache
<RequireAny>
  Require ip 192.168.1.0/24
  Require valid-user
</RequireAny>
```
