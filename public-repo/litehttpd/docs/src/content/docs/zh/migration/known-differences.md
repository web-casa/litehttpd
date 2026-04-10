---
title: 已知差异
description: LiteHTTPD 与 Apache httpd 之间的行为差异
---

## 完全兼容（无差异）

以下功能与 Apache 行为完全一致：
- 全部 80 个支持的指令
- 跨目录层级的指令合并
- AllowOverride 类别过滤
- If/ElseIf/Else 条件表达式求值
- FilesMatch 正则匹配
- RequireAny/RequireAll 授权逻辑

## 轻微差异

### 头名称大小写

| Apache | LiteHTTPD |
|--------|-----------|
| `X-Custom-Header: value` | `x-custom-header: value` |

OLS 将响应头名称转换为小写。根据 HTTP/1.1（RFC 7230），这是合法的，且 HTTP/2 要求如此。无功能性影响。

### Handler 指令（空操作）

`AddHandler`、`SetHandler`、`RemoveHandler` 和 `Action` 会被解析但不会改变请求处理方式。OLS 使用虚拟主机配置中的 `scriptHandler` 代替。

### If 块内的 RewriteRule

```apache
# 不支持
<If "%{REQUEST_URI} =~ /^\/api\//">
  RewriteRule ^api/(.*)$ /handler.php?path=$1 [L]
</If>
```

`<If>` 块内的重写指令会被记录日志并跳过。请将重写规则放在 `.htaccess` 的顶层。

### 连续的 Header append

对同一头名称使用多个 `Header append` 指令时，在某些 OLS 配置中可能只保留最后一个值。建议改用 `Header set` 并设置完整的值。

## OLS 特有行为

### 目录列表

原生 OLS 对没有索引文件的目录返回 404（而非像 Apache 那样返回 200 并显示列表）。使用补丁 0004 和 `Options -Indexes` 后，LiteHTTPD 返回 403，与 Apache 行为一致。

### PHP SAPI

Apache 使用 PHP-FPM（FastCGI），而 OLS 使用 lsphp（LSAPI）。LSAPI 协议效率更高，但进程管理方式不同。参见 [PHP 调优](/zh/performance/php-tuning/) 了解配置详情。

## Thin 版本的额外限制

LiteHTTPD-Thin（在未打补丁的原生 OLS 上运行的 `.so` 模块）相比 Full 版本有以下额外限制：

| 功能 | Full 版本 | Thin 版本 |
|------|----------|----------|
| RewriteRule 执行 | 由打补丁的 OLS 引擎执行 | 解析但不执行；回退到 OLS 原生 `RewriteFile` 处理 |
| php_value / php_flag | 通过 LSIAPI 传递给 lsphp | 解析但无法传递给 lsphp（无 PHPConfig 补丁） |
| Options -Indexes (403) | 返回 403（使用补丁 0004） | 返回 404（原生 OLS 行为） |
| readApacheConf | 启动时自动转换 Apache 配置 | 不可用 |

其他所有指令（Header、Require、FilesMatch、Auth、Expires、SetEnv、If/ElseIf/Else 等）在两个版本中的行为完全一致。
