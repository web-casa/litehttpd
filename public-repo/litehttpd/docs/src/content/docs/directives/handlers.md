---
title: Handlers
description: Handler mapping directives parsed by LiteHTTPD
---

## Directives

| Directive | Syntax | Behavior |
|-----------|--------|----------|
| `AddHandler` | `AddHandler handler-name extension [extension ...]` | Parsed and logged; request handling is controlled by OLS `scriptHandler` config |
| `SetHandler` | `SetHandler handler-name` | Parsed and logged; request handling is controlled by OLS `scriptHandler` config |
| `RemoveHandler` | `RemoveHandler extension [extension ...]` | Parsed and logged; does not remove OLS handler mappings |
| `Action` | `Action media-type cgi-script` | Parsed and logged; CGI action dispatch is not implemented by LiteHTTPD |

## OLS Equivalent

OpenLiteSpeed maps scripts to external apps with `scriptHandler` and `extProcessor` blocks in vhost or server config:

```apacheconf
scriptHandler {
  add                     lsapi:lsphp php
}

extProcessor lsphp {
  type                    lsapi
  address                 uds://tmp/lshttpd/lsphp.sock
  path                    /usr/local/lsws/lsphp84/bin/lsphp
}
```

:::note
These directives are accepted so common Apache `.htaccess` files can be parsed without failing. They are intentionally no-op in request handling because OLS does not expose Apache-style per-directory handler remapping through `.htaccess`.
:::
