---
title: 版本说明
description: LiteHTTPD-Thin 与 LiteHTTPD-Full 的区别和选择
---

LiteHTTPD 提供两个版本，均为免费开源。

## 版本对比

| | LiteHTTPD-Thin | LiteHTTPD-Full |
|-|---------------|----------------|
| **组成** | 仅 `litehttpd_htaccess.so` 模块 | 模块 + 打补丁的 OpenLiteSpeed |
| **安装目标** | 原生（未修改的）OLS | 应用 4 个补丁的定制 OLS |
| **安装时间** | 1 分钟（复制 .so） | 10-15 分钟（编译 OLS） |
| **OLS 二进制** | 不变 | 替换为打补丁版本 |

## 功能对比

| 功能 | LiteHTTPD-Thin | LiteHTTPD-Full |
|------|---------------|----------------|
| Header set/unset/append/merge/add/edit | 支持 | 支持 |
| Require all/ip/env, Order/Allow/Deny | 支持 | 支持 |
| AuthType Basic + AuthUserFile | 支持 | 支持 |
| FilesMatch, Files, Limit/LimitExcept | 支持 | 支持 |
| If/ElseIf/Else 条件块 | 支持 | 支持 |
| Redirect, RedirectMatch | 支持 | 支持 |
| ErrorDocument | 支持 | 支持 |
| ExpiresActive/ExpiresByType/ExpiresDefault | 支持 | 支持 |
| AddType/ForceType/AddCharset | 支持 | 支持 |
| SetEnv/SetEnvIf/BrowserMatch | 支持 | 支持 |
| DirectoryIndex | 支持 | 支持 |
| LSBruteForceProtection | 支持 | 支持 |
| Options -Indexes（模块层面） | 支持 | 支持 |
| **RewriteRule 执行** | 仅解析 | **完整执行** |
| **php_value / php_flag** | 仅解析 | **传递给 lsphp** |
| **Options -Indexes（引擎层面 403）** | 模块回退 | **原生 403** |
| **Apache 配置自动转换** | 手动 | **启动时自动** |

## 如何选择

### LiteHTTPD-Thin

适用于：
- 为原生 OLS 添加安全头、ACL 和缓存
- 不需要 .htaccess 中 RewriteRule 的站点（WordPress 重写由 OLS 原生 `RewriteFile` 处理）
- 快速评估，无需重新编译 OLS
- 使用官方 OLS 镜像的 Docker/容器环境
- 无法修改 OLS 二进制文件的托管服务商

局限性：
- `.htaccess` 中的 `RewriteRule [R=301,L]` 会被解析但不会执行。OLS 回退到原生 `RewriteFile` 处理，可以处理基本的 WordPress 永久链接，但不支持高级重定向规则。
- `php_value` 和 `php_flag` 会被解析但对 lsphp 没有效果。

### LiteHTTPD-Full

适用于：
- 完整的 Apache 到 OLS 迁移，90%+ 兼容性
- 使用 RewriteRule 重定向的 WordPress 插件站点（Redirection、Yoast、Rank Math）
- 使用 `php_value` 进行目录级 PHP 配置的站点
- 替代 Apache 或 LSWS Enterprise 的生产环境

包含 4 个补丁：
- **Patch 0001**（PHPConfig）：通过 LSIAPI 启用 `php_value`/`php_flag`
- **Patch 0002**（Rewrite）：启用完整的 `RewriteRule` 执行
- **Patch 0003**（readApacheConf）：OLS 启动时自动转换 Apache 配置
- **Patch 0004**（autoIndex）：在引擎层面对 `Options -Indexes` 返回 403

## 安装

### LiteHTTPD-Thin

```bash
cp litehttpd_htaccess.so /usr/local/lsws/modules/
echo 'module litehttpd_htaccess { ls_enabled 1 }' >> /usr/local/lsws/conf/httpd_config.conf
/usr/local/lsws/bin/lswsctrl restart
```

### LiteHTTPD-Full (RPM)

```bash
dnf install ./openlitespeed-litehttpd-*.rpm
dnf install ./litehttpd-*.rpm
systemctl restart lsws
```

### LiteHTTPD-Full (从源码)

参见[从源码构建](/zh/development/building/)和[补丁说明](/zh/development/patches/)。
