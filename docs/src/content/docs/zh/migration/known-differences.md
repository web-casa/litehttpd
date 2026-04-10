---
title: 已知差异
description: LiteHTTPD 与 Apache httpd 以及 LiteHTTPD 与原版 OLS 之间的行为差异
---

## LiteHTTPD vs 原版 OLS

以下是在原版 OLS 安装中添加 LiteHTTPD 后引入的行为变化。

### .ht* 文件保护

| | 原版 OLS | LiteHTTPD |
|-|---------|-----------|
| 请求 `.htaccess` | 提供文件内容（200）或文件未找到（404） | **403 禁止** |
| 请求 `.htpasswd` | 同上 | **403 禁止** |

LiteHTTPD 执行 Apache 默认的 `<Files ".ht*"> Require all denied</Files>` 行为。原版 OLS 没有此保护 — `.htaccess` 文件可能被公开读取。

### 路径遍历

| | 原版 OLS | LiteHTTPD |
|-|---------|-----------|
| 编码的 `../`（如 `%2e%2e/`） | 400 或 404（取决于引擎） | **403 禁止** |

LiteHTTPD 增加了针对百分号编码绕过尝试的纵深防御路径遍历检测。

### 之前被忽略的 .htaccess 指令

原版 OLS 忽略大多数 `.htaccess` 指令。添加 LiteHTTPD 后，所有 80 种支持的指令变为活跃状态。如果现有 `.htaccess` 文件包含之前不生效的规则，它们现在会执行：

- `Require all denied` → 阻止访问（403）
- `Header set` → 添加响应头
- `FilesMatch` → 执行访问控制
- `AuthType Basic` → 要求身份验证（401）

**在生产环境部署 LiteHTTPD 前请检查你的 `.htaccess` 文件。**

### Options -Indexes

| | 原版 OLS | LiteHTTPD（Full） | LiteHTTPD（Thin） |
|-|---------|-----------------|-----------------|
| 没有索引文件的目录 | 404 | **403**（需 patch 0004） | 404（不变） |

原版 OLS 对没有索引文件的目录返回 404。LiteHTTPD Full（patch 0004）中 `Options -Indexes` 返回 403，匹配 Apache 行为。

### ExecCGI 被阻止

`.htaccess` 中的 `Options +ExecCGI` 会被 LiteHTTPD 静默忽略，无论哪个版本。Apache 在 `AllowOverride Options` 设置下允许此操作。这是有意为之的安全限制。

---

## LiteHTTPD vs Apache httpd

### 完全兼容（无差异）

以下功能与 Apache 完全一致：
- 所有 80 种支持的指令
- 跨目录级别的指令合并
- AllowOverride 分类过滤
- If/ElseIf/Else 条件表达式求值
- FilesMatch 正则匹配
- RequireAny/RequireAll 授权逻辑

### Header 名称大小写

| Apache | LiteHTTPD |
|--------|-----------|
| `X-Custom-Header: value` | `x-custom-header: value` |

OLS 将响应头名称转为小写。这在 HTTP/1.1（RFC 7230）中是合法的，在 HTTP/2 中是必需的。无功能影响。

### Handler 指令（无操作）

`AddHandler`、`SetHandler`、`RemoveHandler` 和 `Action` 会被解析但不会改变请求处理方式。OLS 使用 vhost 配置中的 `scriptHandler` 进行处理器映射。

### If 块中的 RewriteRule

```apache
# 不支持
<If "%{REQUEST_URI} =~ /^\/api\//">
  RewriteRule ^api/(.*)$ /handler.php?path=$1 [L]
</If>
```

`<If>` 块内的 Rewrite 指令会被记录日志并跳过。请将 rewrite 规则放在 `.htaccess` 的顶级位置。

### 连续 Header append

对同一个 header 名称使用多个 `Header append` 指令，在某些 OLS 配置下可能只保留最后一个值。建议使用 `Header set` 并设置完整值。

### ErrorDocument 本地文件路径（5xx 错误）

对于 `ErrorDocument 404`，LiteHTTPD 正确处理本地文件路径（最常见的用例）。但对于来自 PHP/后端的 5xx 错误，`ErrorDocument 500 /error.html` 无法完全替换响应体，因为 OLS 在模块 hook 触发前已提交 `Content-Length`。请改用外部 URL 重定向：

```apache
# 适用于所有状态码：
ErrorDocument 500 https://example.com/error.html

# 适用于 404（在 URI_MAP 阶段预检查）：
ErrorDocument 404 /error.html

# 可能无法替换 5xx 的响应体（请使用 URL 重定向）：
ErrorDocument 500 /error.html
```

### FollowSymLinks / MultiViews

`Options FollowSymLinks` 和 `Options MultiViews` 会传递给 OLS 引擎，但 OLS 可能不会与 Apache 完全一致地处理它们。如果你的站点依赖这些选项，请测试验证。

---

## OLS 特定行为

### 目录列表

原版 OLS 对没有索引文件的目录返回 404（不像 Apache 返回 200 + 目录列表）。LiteHTTPD Full（patch 0004）+ `Options -Indexes` 返回 403，匹配 Apache 行为。

### PHP SAPI

Apache 通常使用 PHP-FPM（FastCGI），OLS 使用 lsphp（LSAPI）。LSAPI 协议更高效但有不同的进程管理方式。详见 [PHP 性能调优](/zh/performance/php-tuning/)。

### autoLoadHtaccess 双重处理

如果 OLS 原生的 `autoLoadHtaccess` 与 LiteHTTPD 同时启用，两者都理解的指令（`ErrorDocument`、`Options`）可能被双重处理。使用 LiteHTTPD 时请在 vhost 配置中禁用 `autoLoadHtaccess`：

```
autoLoadHtaccess 0
```

---

## Thin 版本的额外限制

LiteHTTPD-Thin（在未打补丁的原版 OLS 上运行的 `.so` 模块）与 Full 版本相比有以下额外限制：

| 功能 | Full 版本 | Thin 版本 |
|------|----------|----------|
| RewriteRule 执行 | 由打补丁的 OLS 引擎执行 | 解析但不执行；回退到 OLS 原生 `RewriteFile` 处理 |
| php_value / php_flag | 通过 LSIAPI 传递给 lsphp | 解析但无法传递给 lsphp（无 PHPConfig 补丁） |
| Options -Indexes (403) | 返回 403（需 Patch 0004） | 返回 404（原版 OLS 行为） |
| readApacheConf | 启动时自动转换 Apache 配置 | 不可用 |

其他所有指令（Header、Require、FilesMatch、Auth、Expires、SetEnv、If/ElseIf/Else 等）在两个版本中完全相同。
