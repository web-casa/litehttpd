---
title: 重定向
description: 重定向指令参考
---

## 指令

| 指令 | 语法 |
|------|------|
| `Redirect` | `Redirect [status] url-path target-URL` |
| `RedirectMatch` | `RedirectMatch [status] regex target-URL` |
| `ErrorDocument` | `ErrorDocument error-code document` |

## 重定向状态码

| 状态码 | 含义 |
|--------|------|
| `301` | 永久移动 |
| `302` | 已找到（默认） |
| `303` | 查看其他 |
| `410` | 已删除 |

## 示例

### 简单重定向

```apache
Redirect 301 /old-page /new-page
Redirect /temp-page /other-page
```

### 正则表达式重定向

```apache
RedirectMatch 301 ^/blog/(.*)$ /articles/$1
RedirectMatch ^/category/(.+)/feed/?$ /rss/$1
```

### 自定义错误页面

```apache
ErrorDocument 404 /custom-404.html
ErrorDocument 403 "Access Denied"
ErrorDocument 500 https://example.com/error
```

:::note
当 `ErrorDocument` 指定的 URL 以 `http://` 或 `https://` 开头时，会发起 302 重定向到该 URL。本地路径则使用原始错误状态码返回内容。
:::
