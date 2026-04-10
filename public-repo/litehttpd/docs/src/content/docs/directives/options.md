---
title: Options
description: Directory options directive
---

## Directive

```apache
Options [+|-]flag [+|-]flag ...
```

## Available Flags

| Flag | Description |
|------|-------------|
| `Indexes` | Allow directory listing when no index file exists |
| `FollowSymLinks` | Follow symbolic links |
| `MultiViews` | Content negotiation |
| `ExecCGI` | Allow CGI execution (blocked in .htaccess) |

Use `+` to enable, `-` to disable. Without prefix, the value replaces inherited settings.

## Examples

### Disable Directory Listing

```apache
Options -Indexes
```

### Combined Options

```apache
Options -Indexes +FollowSymLinks -MultiViews
```

:::note
`ExecCGI` is always blocked in `.htaccess` for security. Use OLS `scriptHandler` configuration instead.
:::
