---
title: Rewrite
description: URL rewriting directives
---

:::caution
RewriteRule **execution** requires [Patch 0002](/development/patches/). Without it, rules are parsed but only processed via OLS native `RewriteFile` mechanism.
:::

## Directives

| Directive | Syntax |
|-----------|--------|
| `RewriteEngine` | `RewriteEngine On\|Off` |
| `RewriteBase` | `RewriteBase /path/` |
| `RewriteCond` | `RewriteCond TestString Pattern [flags]` |
| `RewriteRule` | `RewriteRule Pattern Substitution [flags]` |
| `RewriteOptions` | `RewriteOptions inherit\|IgnoreInherit` |
| `RewriteMap` | `RewriteMap name txt\|rnd\|int:/path` |

## RewriteCond Variables

`%{REQUEST_URI}`, `%{REQUEST_FILENAME}`, `%{QUERY_STRING}`, `%{HTTP_HOST}`, `%{HTTPS}`, `%{REMOTE_ADDR}`, `%{REQUEST_METHOD}`, `%{HTTP_USER_AGENT}`, `%{HTTP_REFERER}`

### Flags

| Flag | Description |
|------|-------------|
| `[NC]` | Case-insensitive match |
| `[OR]` | Combine with next condition using OR (default is AND) |

## RewriteRule Flags

| Flag | Description |
|------|-------------|
| `[L]` | Last rule -- stop processing |
| `[R=301]` | External redirect with status code |
| `[QSA]` | Append query string |
| `[F]` | Forbidden (403) |
| `[G]` | Gone (410) |
| `[NE]` | No escape -- don't encode special characters |

## Examples

### WordPress Permalinks

```apache
RewriteEngine On
RewriteBase /
RewriteRule ^index\.php$ - [L]
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule . /index.php [L]
```

### HTTPS Redirect

```apache
RewriteEngine On
RewriteCond %{HTTPS} off
RewriteRule ^(.*)$ https://%{HTTP_HOST}/$1 [R=301,L]
```

### Remove Trailing Slash

```apache
RewriteEngine On
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule ^(.*)/$ /$1 [R=301,L]
```
