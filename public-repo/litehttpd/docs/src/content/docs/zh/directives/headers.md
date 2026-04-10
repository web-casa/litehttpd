---
title: Header 指令
description: Header 指令指令参考
---

Headers 指令允许你设置、修改和删除 HTTP 响应头和请求头。这些指令对于安全头、CORS 配置、缓存策略等场景至关重要。

## 指令参考

| 指令 | 语法 | 说明 |
|------|------|------|
| `Header set` | `Header [always] set name value [env=VAR]` | 设置响应头，替换任何已有的值 |
| `Header unset` | `Header [always] unset name` | 删除一个响应头 |
| `Header append` | `Header [always] append name value` | 向已有的响应头追加值（以逗号分隔） |
| `Header merge` | `Header [always] merge name value` | 仅在值不存在时追加 |
| `Header add` | `Header [always] add name value` | 添加响应头，即使同名头已存在 |
| `Header edit` | `Header [always] edit name regex replacement` | 使用正则表达式编辑响应头的值（仅匹配第一个） |
| `Header edit*` | `Header [always] edit* name regex replacement` | 使用正则表达式编辑响应头的值（匹配所有） |
| `RequestHeader set` | `RequestHeader set name value` | 设置传递给后端的请求头 |
| `RequestHeader unset` | `RequestHeader unset name` | 在请求到达后端之前删除请求头 |

可选的 `always` 关键字会将头操作应用于所有响应，包括错误响应（4xx、5xx）。不使用 `always` 时，头仅应用于成功的响应。

可选的 `env=VAR` 条件使头操作仅在环境变量 `VAR` 被设置时生效。

## 示例

### 安全头

```apache
Header always set X-Content-Type-Options "nosniff"
Header always set X-Frame-Options "SAMEORIGIN"
Header always set X-XSS-Protection "1; mode=block"
Header always set Referrer-Policy "strict-origin-when-cross-origin"
Header always set Permissions-Policy "camera=(), microphone=(), geolocation=()"
```

### CORS 配置

```apache
Header set Access-Control-Allow-Origin "https://example.com"
Header set Access-Control-Allow-Methods "GET, POST, OPTIONS"
Header set Access-Control-Allow-Headers "Content-Type, Authorization"
```

### 基于环境变量的条件头

```apache
SetEnvIf Request_URI "\.pdf$" is_pdf
Header set Content-Disposition "attachment" env=is_pdf
```

### 使用正则表达式编辑头

```apache
# 从头值中移除 Server 版本信息
Header edit Server "Apache/.*" "Apache"

# 将 Location 头中所有的 http 替换为 https
Header edit* Location "http://" "https://"
```

### 移除不需要的头

```apache
Header unset X-Powered-By
Header always unset Server
RequestHeader unset Proxy
```

:::note
在 OLS 中，`RequestHeader set` 会写入后端（如 lsphp）使用的 `HTTP_*` 环境变量，而不是像 Apache 那样修改原始请求流。
:::
