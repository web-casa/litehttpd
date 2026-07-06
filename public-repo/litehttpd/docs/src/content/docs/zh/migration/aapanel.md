---
title: 从 aaPanel（宝塔面板）迁移
description: 将宝塔面板的 OLS 切换到 LiteHTTPD
---

## 概述

aaPanel（宝塔面板）通过应用商店安装 OLS。使用标准 OLS 路径（`/usr/local/lsws/`），但通过面板 UI 管理配置文件。宝塔面板的 OLS 默认**没有 .htaccess 支持**（仅有基本的 `RewriteFile` 处理）— 这就是很多宝塔 + OLS 用户发现 `.htaccess` 规则不生效的原因。

LiteHTTPD 为宝塔面板的 OLS 添加完整的 `.htaccess` 支持（80 种指令）。

## 安装

```bash
# 添加 LiteHTTPD 仓库并安装
curl -s https://rpms.litehttpd.com/setup.sh | bash
dnf install openlitespeed-litehttpd

# 重启
systemctl restart lsws
```

Thin 模式（不替换二进制）请参见 [从原版 OLS 迁移](/zh/migration/ols-to-litehttpd/)。

## 宝塔面板特别注意事项

### 不要通过应用商店升级 OLS

宝塔面板应用商店的"升级"按钮会重新安装原版 OLS 二进制，覆盖打补丁版本。宝塔面板触发的 OLS 升级后：

```bash
dnf reinstall openlitespeed-litehttpd
systemctl restart lsws
```

### vhost 配置会被重写

宝塔面板在修改站点设置（PHP 版本、SSL、域名别名）时会重新生成 vhost `.conf` 文件。直接添加到 `vhost.conf` 的自定义指令可能会丢失。

**请使用 `.htaccess` 文件管理站点级规则** — 宝塔面板不会修改文档根目录中的文件。

### 面板操作后验证模块配置

宝塔面板在服务器级配置变更时会修改 `httpd_config.conf`。LiteHTTPD 模块配置块通常会保留，但建议在面板大操作后验证：

```bash
grep -c 'litehttpd_htaccess' /usr/local/lsws/conf/httpd_config.conf
# 应输出：1
```

### 禁用 OLS autoLoadHtaccess

如果宝塔面板的 vhost 配置中有 `autoLoadHtaccess 1`，需禁用以避免与 LiteHTTPD 双重处理：

```bash
grep -r 'autoLoadHtaccess' /usr/local/lsws/conf/vhosts/
# 如果有 1，改为 0：
sed -i 's/autoLoadHtaccess.*1/autoLoadHtaccess 0/' /usr/local/lsws/conf/vhosts/*/vhost.conf
```

### PHP-FPM vs LSPHP

部分宝塔面板版本为 OLS 安装了 PHP-FPM 而非 LSPHP。LiteHTTPD 的 `php_value`/`php_flag` 指令（Full 模式）需要 LSPHP（LSAPI 协议）。

检查当前 PHP 类型：

```bash
# 有这个路径说明已安装 LSPHP
ls /usr/local/lsws/lsphp*/bin/lsphp 2>/dev/null

# 检查 vhost 配置中的处理器类型
grep -A5 'extProcessor' /usr/local/lsws/conf/vhosts/*/vhost.conf
```

如果使用的是 PHP-FPM，需切换到 LSPHP：

```bash
dnf install lsphp83 lsphp83-common lsphp83-mysqlnd
```

然后更新 vhost 配置中的外部应用路径为 `/usr/local/lsws/lsphp83/bin/lsphp`，或通过 OLS WebAdmin 控制台（端口 7080）修改。

### 为 WordPress 启用 Rewrite

安装 LiteHTTPD 后，模块会自动处理 `.htaccess` 文件。但 WordPress 固定链接需要在 vhost 中启用 rewrite：

```
# /usr/local/lsws/conf/vhosts/<name>/vhost.conf
rewrite {
  enable                  1
}
```

### 行为变化

安装 LiteHTTPD 后，注意以下变化：

1. **`.htaccess` 文件现在生效了** — 这正是安装的目的，但请检查你的 `.htaccess` 文件。原版 OLS 之前忽略的指令（如 `Header set`、`Require all denied`、`FilesMatch`）现在会执行。

2. **`.ht*` 文件被阻止** — 对 `.htaccess`、`.htpasswd` 的请求返回 403（Apache 兼容安全机制）。原版 OLS 可能会直接提供文件或返回 404。

3. **路径遍历被阻止** — 编码的 `../` 序列返回 403。

## 验证

```bash
# 测试 .htaccess 处理
echo 'Header set X-LiteHTTPD "active"' > /www/wwwroot/example.com/.htaccess
curl -sI https://example.com/ | grep X-LiteHTTPD
# 预期：X-LiteHTTPD: active

# 清理
rm /www/wwwroot/example.com/.htaccess
```
