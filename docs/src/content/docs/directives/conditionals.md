---
title: Conditionals
description: If/ElseIf/Else conditional block directives
---

## Directives

| Directive | Syntax |
|-----------|--------|
| `<If>` | `<If "expression">` |
| `<ElseIf>` | `<ElseIf "expression">` |
| `<Else>` | `<Else>` |

## Expression Operators

| Operator | Description | Example |
|----------|-------------|---------|
| `==` | String equality | `"%{REQUEST_URI}" == "/admin"` |
| `!=` | String inequality | `"%{HTTPS}" != "on"` |
| `=~` | Regex match | `"%{REQUEST_URI}" =~ /\.php$/` |
| `!~` | Regex not match | `"%{HTTP_USER_AGENT}" !~ /bot/i` |
| `-f` | File exists | `-f "%{REQUEST_FILENAME}"` |
| `-d` | Directory exists | `-d "%{REQUEST_FILENAME}"` |
| `-s` | File exists and not empty | `-s "%{REQUEST_FILENAME}"` |
| `-l` | Is symlink | `-l "%{REQUEST_FILENAME}"` |
| `-ipmatch` | IP/CIDR match | `"-ipmatch" "192.168.0.0/16"` |
| `&&` | Logical AND | |
| `\|\|` | Logical OR | |
| `!` | Logical NOT | |

## Examples

### Environment-based Headers

```apache
<If "%{HTTPS} == 'on'">
  Header set Strict-Transport-Security "max-age=31536000"
</If>
```

### Conditional Access

```apache
<If "-ipmatch '10.0.0.0/8'">
  Require all granted
</If>
<Else>
  Require valid-user
</Else>
```

### Request Method Check

```apache
<If "%{REQUEST_METHOD} == 'OPTIONS'">
  Header set Access-Control-Allow-Origin "*"
  Header set Access-Control-Allow-Methods "GET, POST, OPTIONS"
</If>
```

:::caution
RewriteRule directives inside `<If>` blocks are not supported. Place rewrite rules at the top level of your .htaccess file.
:::
