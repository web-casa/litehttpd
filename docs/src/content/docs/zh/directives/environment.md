---
title: 环境变量
description: 环境变量指令参考
---

## 指令

| 指令 | 语法 |
|------|------|
| `SetEnv` | `SetEnv name value` |
| `SetEnvIf` | `SetEnvIf attribute regex [!]var[=val]` |
| `SetEnvIfNoCase` | `SetEnvIfNoCase attribute regex [!]var[=val]` |
| `BrowserMatch` | `BrowserMatch regex [!]var[=val]` |

## SetEnvIf 属性

`Remote_Addr`, `Remote_Host`, `Request_URI`, `Request_Method`, `HTTP_USER_AGENT`, `HTTP_REFERER`, `HTTP_HOST`，或任何 HTTP 请求头名称。

## 示例

### 设置环境变量

```apache
SetEnv APPLICATION_ENV production
SetEnv DB_HOST localhost
```

### 条件变量

```apache
SetEnvIf Remote_Addr "^192\.168\." local_network
SetEnvIf Request_URI "\.pdf$" is_pdf
SetEnvIfNoCase User-Agent "bot" is_bot
BrowserMatch "MSIE" ie_browser
```

### 与 Header 指令配合使用

```apache
SetEnvIf Request_URI "\.pdf$" is_download
Header set Content-Disposition "attachment" env=is_download
```
