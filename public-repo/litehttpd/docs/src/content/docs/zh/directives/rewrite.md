---
title: URL 重写
description: URL 重写指令参考
---

:::caution
RewriteRule 的**执行**需要 [Patch 0002](/zh/development/patches/)。如果没有该补丁，规则会被解析但仅通过 OLS 原生的 `RewriteFile` 机制处理。
:::

## 指令

| 指令 | 语法 |
|------|------|
| `RewriteEngine` | `RewriteEngine On\|Off` |
| `RewriteBase` | `RewriteBase /path/` |
| `RewriteCond` | `RewriteCond TestString Pattern [flags]` |
| `RewriteRule` | `RewriteRule Pattern Substitution [flags]` |
| `RewriteOptions` | `RewriteOptions inherit\|IgnoreInherit` |
| `RewriteMap` | `RewriteMap name txt\|rnd\|int:/path` |

## RewriteCond 变量

`%{REQUEST_URI}`, `%{REQUEST_FILENAME}`, `%{QUERY_STRING}`, `%{HTTP_HOST}`, `%{HTTPS}`, `%{REMOTE_ADDR}`, `%{REQUEST_METHOD}`, `%{HTTP_USER_AGENT}`, `%{HTTP_REFERER}`

### 标志

| 标志 | 说明 |
|------|------|
| `[NC]` | 不区分大小写匹配 |
| `[OR]` | 与下一个条件使用 OR 组合（默认为 AND） |

## RewriteRule 标志

| 标志 | 说明 |
|------|------|
| `[L]` | 最后一条规则——停止处理 |
| `[R=301]` | 带状态码的外部重定向 |
| `[QSA]` | 追加查询字符串 |
| `[F]` | 禁止访问 (403) |
| `[G]` | 已删除 (410) |
| `[NE]` | 不转义——不编码特殊字符 |

## 示例

### WordPress 固定链接

```apache
RewriteEngine On
RewriteBase /
RewriteRule ^index\.php$ - [L]
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule . /index.php [L]
```

### HTTPS 重定向

```apache
RewriteEngine On
RewriteCond %{HTTPS} off
RewriteRule ^(.*)$ https://%{HTTP_HOST}/$1 [R=301,L]
```

### 移除末尾斜杠

```apache
RewriteEngine On
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule ^(.*)/$ /$1 [R=301,L]
```
