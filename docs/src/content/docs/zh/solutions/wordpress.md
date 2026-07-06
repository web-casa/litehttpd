---
title: WordPress 托管
description: OpenLiteSpeed 上 WordPress 的完整 .htaccess 支持
---

## 问题

WordPress 插件会为安全、缓存、SEO 和图片优化生成复杂的 `.htaccess` 规则。原生 OpenLiteSpeed 会忽略大部分这些指令，导致站点缺乏保护且配置失效。

:::note
完整的 WordPress .htaccess 兼容性 -- 包括 RewriteRule 执行和 php_value/php_flag -- 需要 LiteHTTPD（Full 版本，通过 `openlitespeed-litehttpd` 安装）。LiteHTTPD-Thin 可处理安全头、缓存和 ACL 指令，但 RewriteRule 重定向和 PHP INI 覆盖需要应用 OLS 补丁的 Full 版本。
:::

## 已测试 15 个热门插件

LiteHTTPD 已通过最流行的写入 `.htaccess` 规则的 WordPress 插件测试：

| 类别 | 插件 | 使用的 .htaccess 指令 |
|------|------|----------------------|
| 安全 | All-In-One Security, Wordfence | FilesMatch, Require, Order/Deny, Files |
| 缓存 | W3 Total Cache, WP Super Cache, Far Future Expiry | ExpiresByType, Header set Cache-Control, AddType |
| SEO | Yoast SEO, Rank Math | Redirect, RewriteRule |
| SSL | Really Simple SSL | RewriteCond %{HTTPS}, RewriteRule [R=301] |
| 图片 | WebP Express, EWWW, Imagify | RewriteCond %{HTTP_ACCEPT}, AddType |
| 响应头 | HTTP Headers | Header set (CSP, X-Frame-Options) |
| 缓存 (OLS) | LiteSpeed Cache | CacheLookup, RewriteRule |
| 隐藏 | WP Hide & Security Enhancer | RewriteRule 路径混淆 |

全部 15 个插件均通过兼容性测试，行为与 Apache httpd 完全一致。

## 开箱即用的功能

- WordPress 固定链接重写规则
- 安全插件 IP 封锁和文件保护
- 缓存插件 Expires 和 Cache-Control 响应头
- SEO 插件重定向和站点地图规则
- SSL/HTTPS 强制跳转
- 自定义错误页面 (ErrorDocument)
- PHP 配置 (php_value, php_flag)
- 目录索引设置
- 文件上传限制

## 快速设置

```bash
# 通过 RPM 安装 LiteHTTPD（Full 版本）
dnf install ./openlitespeed-litehttpd-*.rpm

# 或手动安装模块
cp litehttpd_htaccess.so /usr/local/lsws/modules/

# Enable in OLS config
echo 'module litehttpd_htaccess { ls_enabled 1 }' >> /usr/local/lsws/conf/httpd_config.conf

# Enable .htaccess for WordPress vhost
echo -e "allowOverride 255\nautoLoadHtaccess 1" >> /usr/local/lsws/conf/vhosts/wordpress/vhconf.conf

# Restart
/usr/local/lsws/bin/lswsctrl restart
```

## 与 Apache 的性能对比

在启用全部 15 个插件的情况下运行 WordPress 6.9（217 行 .htaccess）：

| 指标 | Apache httpd | LiteHTTPD |
|------|-------------|-----------|
| 静态文件 RPS | 10,618 | **21,960**（快 2.1 倍） |
| .htaccess 解析开销 | -4.2% | **-0.7%** |
| 基线内存 | 969 MB | **676 MB**（减少 30%） |
| .htaccess 兼容性 | 100% | 90%+ |
