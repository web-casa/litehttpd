---
title: 常见问题
description: LiteHTTPD 常见问题解答
---

## 基本问题

### 修改 .htaccess 后需要重启 OLS 吗？

**不需要。** 模块在每次请求时检查 `.htaccess` 文件的修改时间，文件变化后下一个请求立即生效，无需重启。

所有指令都是如此：Header、Require、Redirect、RewriteRule、php_value、ExpiresActive、ErrorDocument 等。

唯一的例外是 **OLS vhost 级别的 rewrite 规则**（在 `vhconf.conf` 中），需要 `lswsctrl restart`。WordPress Permalink 的 fallback 路由需要这个——详见 [故障排除](/zh/getting-started/configuration/#wordpress-permalinks-return-404)。

### 不同目录可以放不同的 .htaccess 吗？

**可以。** 这是 `.htaccess` 的核心设计——目录级层叠覆盖。模块从文档根目录到请求文件所在目录逐级查找 `.htaccess`，合并规则，子目录覆盖父目录。

示例：

```
/var/www/html/.htaccess              ← 全站规则
/var/www/html/admin/.htaccess        ← 管理区规则（覆盖全站）
/var/www/html/admin/api/.htaccess    ← API 规则（覆盖管理区和全站）
```

```apache
# /var/www/html/.htaccess
Header set X-Frame-Options "SAMEORIGIN"
ExpiresActive On

# /var/www/html/admin/.htaccess
Require all denied
Header set X-Area "admin"

# /var/www/html/admin/api/.htaccess
Require ip 10.0.0.0/8
php_value memory_limit 512M
```

结果：
- 访问 `/index.html` → X-Frame-Options + 缓存头（仅全站规则）
- 访问 `/admin/dashboard.php` → **403 禁止**（被 admin .htaccess 拒绝）
- 从 `10.0.0.1` 访问 `/admin/api/data.json` → **200 OK** + 512M 内存限制
- 从外网访问 `/admin/api/data.json` → **403 禁止**

每个目录的 `.htaccess` 独立生效，修改后立即生效，无需重启。

---

## WordPress

### 为什么 WordPress 固定链接返回 404？

OLS 默认的 vhost 配置中 `rewrite { enable 0 }`。`.htaccess` 中的 RewriteRule 会被 LiteHTTPD 解析，但 OLS 的 rewrite 引擎必须在 vhost 级别启用。

**修复方法：**

1. 编辑 vhost 配置（如 `/usr/local/lsws/conf/vhosts/Example/vhconf.conf`）：

```
rewrite {
  enable                  1
}
```

2. 在 vhost context 中添加 fallback rewrite 规则：

```
context / {
  rewrite {
    RewriteFile .htaccess
    rules <<<END_rules
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule ^(.*)$ /index.php [L]
END_rules
  }
}
```

3. 重启 OLS：`/usr/local/lsws/bin/lswsctrl restart`

> 如果通过 RPM 仓库安装（`dnf install openlitespeed-litehttpd`），步骤 1 和 index.php 添加会自动完成。

### 依赖 .htaccess 的 WordPress 插件能用吗？

**能。** 我们测试了 10 个主流 .htaccess 依赖插件，全部与 Apache 结果一致：

| 插件 | 状态 |
|------|------|
| Wordfence Security | ✅ 正常 |
| All In One WP Security | ✅ 正常 |
| W3 Total Cache | ✅ 正常 |
| WP Super Cache | ✅ 正常 |
| Yoast SEO | ✅ 正常 |
| Redirection | ✅ 正常 |
| WPS Hide Login | ✅ 正常 |
| Disable XML-RPC | ✅ 正常 |
| iThemes Security | ✅ 正常 |
| HTTP Headers | ✅ 正常 |

### .htaccess 中的 php_value 和 php_flag 能用吗？

**Full 模式可以。** 值通过原生 PHPConfig LSIAPI 传递给 lsphp。

```apache
php_value upload_max_filesize 64M
php_value post_max_size 64M
php_flag display_errors Off
```

Thin 模式下，`php_value`/`php_flag` 会被解析但不执行。

> **注意：** `php_admin_value` 和 `php_admin_flag` 在 `.htaccess` 中被**禁止**，因为可能覆盖 `disable_functions`、`open_basedir` 等安全设置。请使用 OLS 管理面板或 `php.ini` 配置管理员级 PHP 设置。

---

## 性能

### LiteHTTPD 和 Apache 性能对比如何？

在 Linode 4C/8GB VPS 上，WordPress + PHP 8.3：

| 指标 | Apache 2.4 | OLS + LiteHTTPD |
|------|-----------|-----------------|
| 静态文件 RPS | 23,909 | **58,891**（快 2.5 倍） |
| PHP RPS (wp-login) | 274 | **292**（快 1.07 倍） |
| Server RSS | 618 MB | **449 MB**（少 27%） |
| .htaccess 兼容性 | 10/10 | **10/10** |

### .htaccess 处理会拖慢请求吗？

开销极小——每个请求不到 0.5ms。模块使用文件 stat 缓存（mtime 变化才重新读取）、解析指令缓存、SHM 共享内存等优化。

---

## 安全

### wp-config.php 是否受保护？

**是的，前提是你有正确的 .htaccess 规则。** 在 WordPress 的 `.htaccess` 中添加：

```apache
<Files "wp-config.php">
Require all denied
</Files>
```

没有 LiteHTTPD 的 stock OLS **不处理** `<Files>` 指令，会导致 wp-config.php 可被直接访问（返回 200，暴露数据库密码）。

---

## 安装

### Full 模式和 Thin 模式有什么区别？

| | Full 模式 | Thin 模式 |
|-|-----------|-----------|
| **OLS 二进制** | 打补丁版（4 个 patch） | 原版（未修改） |
| **指令数** | 80（全部功能） | 70+（无 RewriteRule 执行、无 php_value） |
| **安装** | `dnf install openlitespeed-litehttpd` | 复制 `.so` 文件 |

### 应该安装什么 PHP？

LiteHTTPD 不捆绑 PHP，你可以选择：

- **lsphp（LiteSpeed 仓库）** — `dnf install lsphp83`（推荐）
- **php-litespeed（Remi 仓库）** — `dnf install php-litespeed`
- **任何 LSAPI 兼容的 PHP** — 配置 `httpd_config.conf` 中的 `extProcessor` 路径

### 如何从 CyberPanel / aaPanel / Docker 切换到 LiteHTTPD？

详见迁移指南：
- [从原版 OLS 迁移](/zh/migration/ols-to-litehttpd/)
- [从 CyberPanel 迁移](/zh/migration/cyberpanel/)
- [从 aaPanel 迁移](/zh/migration/aapanel/)
- [Docker 环境](/zh/migration/docker/)
