---
title: Redirects
description: URL redirect and error document directives
---

## Directives

| Directive | Syntax |
|-----------|--------|
| `Redirect` | `Redirect [status] url-path target-URL` |
| `RedirectMatch` | `RedirectMatch [status] regex target-URL` |
| `ErrorDocument` | `ErrorDocument error-code document` |

## Redirect Status Codes

| Status | Meaning |
|--------|---------|
| `301` | Moved Permanently |
| `302` | Found (default) |
| `303` | See Other |
| `410` | Gone |

## Examples

### Simple Redirect

```apache
Redirect 301 /old-page /new-page
Redirect /temp-page /other-page
```

### Regex Redirect

```apache
RedirectMatch 301 ^/blog/(.*)$ /articles/$1
RedirectMatch ^/category/(.+)/feed/?$ /rss/$1
```

### Error Documents

```apache
ErrorDocument 404 /custom-404.html
ErrorDocument 403 "Access Denied"
ErrorDocument 500 https://example.com/error
```

:::note
When `ErrorDocument` specifies a URL starting with `http://` or `https://`, it issues a 302 redirect to that URL. Local paths serve content with the original error status code.
:::
