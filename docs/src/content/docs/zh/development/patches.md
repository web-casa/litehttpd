---
title: OLS 补丁
description: 用于启用高级功能的可选 OpenLiteSpeed 补丁
---

这些补丁是 LiteHTTPD-Full 与 LiteHTTPD-Thin 的区别所在。LiteHTTPD 可与原生 OLS 配合使用（Thin 版本），但这 4 个补丁可启用额外功能，构成 Full 版本。

## 补丁概览

| 补丁 | 功能 | 用途 |
|-------|---------|-------------|
| 0001 | PHPConfig LSIAPI | php_value, php_flag 指令 |
| 0002 | RewriteRule LSIAPI | RewriteRule 执行（不仅仅是解析） |
| 0003 | readApacheConf | 启动时自动转换 Apache 配置 |
| 0004 | autoIndex 403 | Options -Indexes 返回 403 |

## 补丁 0001：PHPConfig LSIAPI 扩展

向 `lsi_api_t` 添加 PHP 配置的函数指针：

```c
int (*set_php_config_value)(session, name, value, type);
int (*set_php_config_flag)(session, name, int_value, type);
int (*get_php_config)(session, name, buf, buf_len);
int (*set_req_header)(session, name, name_len, value, value_len, op);
```

**修改的文件**：`include/ls.h`, `src/lsiapi/lsiapilib.cpp`

没有此补丁时，`php_value` 和 `php_flag` 指令会被解析但无法传递给 lsphp。

## 补丁 0002：RewriteRule LSIAPI 扩展

向模块添加重写引擎访问接口：

```c
void *(*parse_rewrite_rules)(rules_text, text_len);
int (*exec_rewrite_rules)(session, handle, base, base_len);
void (*free_rewrite_rules)(handle);
```

同时向 `RewriteEngine` 类添加 `getStatusCode()` 公共访问器。

**修改的文件**：`include/ls.h`, `src/http/rewriteengine.h`, `src/lsiapi/lsiapilib.cpp`

没有此补丁时，RewriteRule 指令会被解析但不会执行。模块将回退到 OLS 原生的 `RewriteFile .htaccess` 处理方式。

## 补丁 0003：readApacheConf 启动钩子

向 OLS plainconf 添加 `readApacheConf` 指令，在启动时触发 `litehttpd-confconv`：

```
readApacheConf /etc/httpd/conf/httpd.conf portmap=80:8088,443:8443
```

**修改的文件**：`src/main/plainconf.cpp`

## 补丁 0004：autoIndex 403

当 `.htaccess` 包含 `Options -Indexes` 且目录没有索引文件时，此补丁返回 403 Forbidden 而非显示目录列表或返回 404。

**修改的文件**：`src/http/httpreq.cpp`

该补丁在 `HttpReq::processPath()` 的 contextMap 阶段扫描 `.htaccess`，使用正确的 `+Indexes`/`-Indexes` 优先级解析 `Options` 令牌。

## 应用补丁

```bash
cd /path/to/openlitespeed-1.8.5
patch -p1 < /path/to/litehttpd/patches/0001-lsiapi-phpconfig.patch
patch -p1 < /path/to/litehttpd/patches/0002-lsiapi-rewrite.patch
# 补丁 0003 和 0004 需要手动应用（Python 脚本）
bash build.sh
```

## ABI 兼容性

所有补丁都在现有结构的末尾追加 -- 从不修改已有的字段偏移。这确保：
- 使用打补丁头文件编译的模块可与打补丁的 OLS 配合使用
- 原生 OLS 模块继续正常工作（新字段只是未被使用）
- 模块在运行时通过 NULL 指针检查检测补丁可用性
