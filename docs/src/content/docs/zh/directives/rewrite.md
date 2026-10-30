---
title: URL 重写
description: URL 重写 —— 原生执行引擎与指令参考
---

重写规则在**原版和打过补丁的** OpenLiteSpeed 上都能**原生执行**。补丁版
OLS（"Full" 版）走 LSI-API 快路径；原版 OLS 由模块内置引擎直接执行
Apache mod_rewrite 子集——无需补丁。

## 指令

| 指令 | 语法 |
|------|------|
| `RewriteEngine` | `RewriteEngine On\|Off` |
| `RewriteBase` | `RewriteBase /path/` |
| `RewriteCond` | `RewriteCond TestString Pattern [flags]` |
| `RewriteRule` | `RewriteRule Pattern Substitution [flags]` |
| `RewriteOptions` | `RewriteOptions inherit\|IgnoreInherit` |
| `RewriteMap` | `RewriteMap name txt\|rnd\|int:/path` |

:::note[引擎选择]
补丁版 OLS 通过 ABI 标记自动识别并优先使用。原版 OLS 运行内置引擎。
所用引擎对用户透明——同一份 `.htaccess` 在两种环境都能工作。
:::

## Pattern 语法（内置引擎）

- PCRE2 正则（与模块各处使用的同一引擎，带 `match_limit` 防病态回溯）
- `.` 同样匹配换行（Apache 重写语义）
- 前导 `!` 表示否定——注意否定匹配**不产生捕获组**供 `$N` 使用

## Substitution

| 形式 | 行为 |
|------|------|
| `-` | URI 不变（配合 `[F]`/`[G]` 或纯条件规则） |
| `/path` | 内部重写 |
| `/path?query` | 带新查询串的内部重写 |
| `http(s)://…` | 外部重定向 |
| 相对路径 | 拼接 `RewriteBase` 前缀 |

替换串内可展开：

- `$0` … `$9` —— 规则 pattern 的捕获组
- `%{VAR}` —— 服务器变量（与 RewriteCond 同集，见下）

## RewriteRule 标志

| 标志 | 效果 |
|------|------|
| `[L]`、`[END]` | 停止处理后续规则 |
| `[R]` / `[R=301..308]` | 外部重定向（裸 `[R]` 默认 302） |
| `[NC]` | 不区分大小写 |
| `[QSA]` | 追加原始查询串 |
| `[F]` | 403 禁止（substitution 用 `-`） |
| `[G]` | 410 已删除 |
| `[NE]`、`[PT]` | 接受但在此子集中为空操作 |

## RewriteCond

测试串可引用 `%{VAR}` 服务器变量：

`%{REQUEST_URI}`, `%{REQUEST_FILENAME}`, `%{QUERY_STRING}`,
`%{HTTP_HOST}`, `%{HTTPS}`, `%{REMOTE_ADDR}`, `%{REQUEST_METHOD}`,
`%{HTTP_USER_AGENT}`, `%{HTTP_REFERER}`, `%{HTTP:Header}`, `%{ENV:var}`

CondPattern 形式：

| 形式 | 含义 |
|------|------|
| PCRE2 正则 | 匹配展开后的测试串 |
| `!pattern` | 否定 |
| `-f` / `-d` / `-s` | 文件存在 / 是目录 / 非空（相对路径按文档根解析） |
| `<` `>` `=` | 与字面操作数的字典序比较 |

Cond 标志：`[NC]`、`[OR]`（OR 组内任一满足即通过；默认 AND 组合）。

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

### 强制 HTTPS

```apache
RewriteEngine On
RewriteCond %{HTTPS} off
RewriteRule ^(.*)$ https://%{HTTP_HOST}/$1 [R=301,L]
```

### 带捕获组的重定向

```apache
RewriteEngine On
RewriteRule ^/old/(.*)$ /new/$1 [L]
```

### 封禁路径

```apache
RewriteEngine On
RewriteRule ^/secret - [F]
```

## 与 Apache 的文档化差异

- 规则按文件序**单遍**执行；内部重写后**不**从头重扫（Apache 的目录
  上下文会重新扫描）。主流真实场景——单一 catch-all 前端控制器——行为
  完全一致。
- RewriteCond 测试串中的 `$N` 反向引用在首条规则匹配前近似为空。
- 替换串中的 `%N` 条件组引用 v1 中预留未填充。
