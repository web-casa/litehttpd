---
title: 缓存
description: 缓存指令参考
---

## 指令

| 指令 | 语法 |
|------|------|
| `ExpiresActive` | `ExpiresActive On\|Off` |
| `ExpiresByType` | `ExpiresByType MIME-type "base plus num unit"` |
| `ExpiresDefault` | `ExpiresDefault "base plus num unit"` |

## 时间单位

`years`, `months`, `weeks`, `days`, `hours`, `minutes`, `seconds`

## 基准值

| 基准 | 含义 |
|------|------|
| `access` | 客户端访问时间（请求时间） |
| `modification` | 文件修改时间（在 OLS 中按 access 处理） |

## 示例

### 标准缓存头

```apache
ExpiresActive On
ExpiresByType text/css "access plus 1 year"
ExpiresByType application/javascript "access plus 1 year"
ExpiresByType image/jpeg "access plus 1 month"
ExpiresByType image/png "access plus 1 month"
ExpiresByType image/webp "access plus 1 month"
ExpiresByType font/woff2 "access plus 1 year"
ExpiresDefault "access plus 2 days"
```

这会自动生成 `Cache-Control: max-age=N` 和 `Expires:` 响应头。

### W3 Total Cache 风格

```apache
ExpiresActive On
ExpiresByType text/css "access plus 31536000 seconds"
ExpiresByType application/x-javascript "access plus 31536000 seconds"
ExpiresByType image/gif "access plus 31536000 seconds"
```
