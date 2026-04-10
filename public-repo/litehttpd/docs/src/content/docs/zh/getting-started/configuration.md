---
title: 配置
description: 如何配置 LiteHTTPD 模块设置
---

## 模块配置（所有版本）

添加到 `/usr/local/lsws/conf/httpd_config.conf`：

```
module litehttpd_htaccess {
    ls_enabled              1
}
```

添加到你的虚拟主机配置（`/usr/local/lsws/conf/vhosts/<name>/vhconf.conf`）：

```
allowOverride 255
autoLoadHtaccess 1
```

### AllowOverride 值

| 值 | 类别 | 指令 |
|-------|----------|-----------|
| `1` | Limit | Order, Allow, Deny |
| `2` | Auth | AuthType, AuthName, Require, Satisfy |
| `4` | FileInfo | AddType, Redirect, Rewrite, ErrorDocument |
| `8` | Indexes | Options Indexes |
| `16` | Options | Options（非 Indexes）, Expires, Limit |
| `255` | All | 所有指令（推荐） |

## 完整版配置

### RewriteRule 执行

当检测到打补丁的 OLS 二进制文件时，RewriteRule 执行会自动启用，无需额外配置。

### php_value / php_flag

使用打补丁的 OLS 二进制文件时，`.htaccess` 中的 `php_value` 和 `php_flag` 指令会通过 LSIAPI PHPConfig API 传递给 lsphp。

### Apache 配置自动转换（补丁 0003）

`readApacheConf` 指令可在启动时将现有的 Apache httpd.conf 转换为 OLS 原生配置：

```
readApacheConf /etc/httpd/conf/httpd.conf portmap=80:8088
```

## PHP 性能调优（所有版本）

对于生产环境部署，配置 lsphp 工作进程池：

```
extProcessor lsphp {
    env                     PHP_LSAPI_CHILDREN=10
    env                     PHP_LSAPI_MAX_IDLE=300
}
```

这将预启动 10 个 PHP 工作进程，消除冷启动延迟。

## Thin 版本限制

使用 LiteHTTPD-Thin（仅模块，原生 OLS 二进制文件）时：

- `RewriteRule` 指令会被解析但不会执行——请求将回退到 OLS 原生路由
- `php_value` 和 `php_flag` 会被解析但不会传递给 lsphp
- `Options -Indexes` 会作为提示存储但不会在引擎级别强制执行
- `readApacheConf` 不可用
