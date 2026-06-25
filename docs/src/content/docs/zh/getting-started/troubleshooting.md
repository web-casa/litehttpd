---
title: 故障排查
description: LiteHTTPD 安装和配置中的常见问题
---

修改应用代码前，先检查这些基础项：

```bash
grep -n "litehttpd_htaccess" /usr/local/lsws/conf/httpd_config.conf
grep -n "allowOverride\\|autoLoadHtaccess" /usr/local/lsws/conf/vhosts/*/vhconf.conf
/usr/local/lsws/bin/lswsctrl restart
tail -n 100 /usr/local/lsws/logs/error.log
```

## RPM 配置脚本拒绝当前服务器

RPM 仓库配置脚本支持 EL 8/9/10 的 x86_64 环境。如果提示架构不支持，请在该平台使用源码构建或模块安装。

## `.htaccess` 修改不生效

先检查 vhost 配置：

```apacheconf
allowOverride 255
autoLoadHtaccess 0
```

再确认 `httpd_config.conf` 中已启用模块：

```apacheconf
module litehttpd_htaccess {
    ls_enabled              1
}
```

修改模块或 vhost 设置后重启 OLS。只做 graceful reload 不一定足够。

## 指令像是被执行了两次

如果 vhost 仍有 `autoLoadHtaccess 1`，OLS 原生 `.htaccess` 解析和 LiteHTTPD 可能同时处理 `ErrorDocument`、`Options` 等重叠指令。改为：

```apacheconf
autoLoadHtaccess 0
```

LiteHTTPD 仍会通过自己的钩子加载 `.htaccess`。

## RewriteRule 重定向不工作

LiteHTTPD-Thin 会解析 `RewriteRule`，但原生 OLS 不会执行 LiteHTTPD 的重写句柄。当插件或应用依赖 `.htaccess` 重写执行时，请使用 LiteHTTPD-Full，尤其是带 `[R=301,L,QSA,F,G]` 等标记的重定向规则。

## `php_value` 或 `php_flag` 不生效

这些指令需要 LiteHTTPD-Full。Thin 模式只会为兼容性解析它们，不会传递给 lsphp。

同时确认站点实际使用 lsphp/LSAPI。PHP-FPM 或代理形式的 PHP 路由不会接收 LSIAPI PHPConfig 值。

## AddHandler 或 SetHandler 没有改变 PHP 处理方式

`AddHandler`、`SetHandler`、`RemoveHandler` 和 `Action` 只是兼容性解析。OLS 的请求处理通过 `scriptHandler` 和 `extProcessor` 配置，不使用 Apache 的逐目录处理器重映射。

## 站点启用后返回 403

LiteHTTPD 会让此前被忽略的安全规则生效。检查：

- 包含 `Require all denied` 的 `<Files>` 或 `<FilesMatch>` 块
- `Require ip` 或旧式 `Order` / `Allow` / `Deny` 规则
- `Options -Indexes`

对于 `wp-config.php` 等受保护文件，这通常是预期结果。如果普通路由返回 403，请从文档根目录到请求路径逐级检查最近的 `.htaccess` 文件。

## WordPress 固定链接很慢

这通常是 OLS 的 PHP 路由开销，不是 `.htaccess` 解析开销。对可缓存页面使用 LSCache，并确认静态资源由 OLS 直接提供。
