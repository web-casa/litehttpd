---
title: Environment
description: Environment variable directives
---

## Directives

| Directive | Syntax |
|-----------|--------|
| `SetEnv` | `SetEnv name value` |
| `SetEnvIf` | `SetEnvIf attribute regex [!]var[=val]` |
| `SetEnvIfNoCase` | `SetEnvIfNoCase attribute regex [!]var[=val]` |
| `BrowserMatch` | `BrowserMatch regex [!]var[=val]` |

## SetEnvIf Attributes

`Remote_Addr`, `Remote_Host`, `Request_URI`, `Request_Method`, `HTTP_USER_AGENT`, `HTTP_REFERER`, `HTTP_HOST`, or any HTTP request header name.

## Examples

### Set Environment Variables

```apache
SetEnv APPLICATION_ENV production
SetEnv DB_HOST localhost
```

### Conditional Variables

```apache
SetEnvIf Remote_Addr "^192\.168\." local_network
SetEnvIf Request_URI "\.pdf$" is_pdf
SetEnvIfNoCase User-Agent "bot" is_bot
BrowserMatch "MSIE" ie_browser
```

### Use with Header Directives

```apache
SetEnvIf Request_URI "\.pdf$" is_download
Header set Content-Disposition "attachment" env=is_download
```
