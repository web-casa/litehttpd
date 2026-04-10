---
title: WordPress 迁移
description: 将 WordPress 从 Apache 迁移到 OpenLiteSpeed 并使用 LiteHTTPD
---

## WordPress .htaccess 兼容性

LiteHTTPD 在安装了 19 个流行插件的情况下，通过了全部 18 项 WordPress 兼容性测试：

- All-In-One Security (AIOS)
- Wordfence Security
- W3 Total Cache
- WP Super Cache
- Yoast SEO
- Rank Math SEO
- Redirection
- Really Simple SSL
- HTTP Headers
- WebP Express
- EWWW Image Optimizer
- Imagify
- Far Future Expiry Header
- WP Hide & Security Enhancer
- LiteSpeed Cache

## 标准 WordPress .htaccess

默认的 WordPress `.htaccess` 无需修改即可工作：

```apache
# BEGIN WordPress
RewriteEngine On
RewriteBase /
RewriteRule ^index\.php$ - [L]
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule . /index.php [L]
# END WordPress
```

:::note
RewriteRule 执行需要 [补丁 0002](/zh/development/patches/)。没有该补丁时，OLS 通过其原生的 `RewriteFile` 机制处理重写规则，可以处理基本的 WordPress 路由，但可能不支持所有标志组合。
:::

## 插件生成的规则

WordPress 插件通常会在 `.htaccess` 中添加以下类型的规则：

| 插件类型 | 使用的指令 |
|------------|----------------|
| 安全（AIOS、Wordfence） | `<FilesMatch>`, `Require`, `Order/Deny`, `<Files>` |
| 缓存（W3TC、WPSC） | `ExpiresByType`, `Header set Cache-Control`, `AddType` |
| SEO（Yoast、Rank Math） | `Redirect`, `RewriteRule` |
| SSL（Really Simple SSL） | `RewriteCond %{HTTPS}`, `RewriteRule [R=301]` |
| 图片（WebP Express） | `RewriteCond %{HTTP_ACCEPT}`, `AddType` |

以上所有指令均被 LiteHTTPD 完全支持。

## PHP 配置

在 `.htaccess` 中设置 WordPress 特定的 PHP 配置：

```apache
php_value upload_max_filesize 64M
php_value post_max_size 64M
php_value memory_limit 256M
php_flag display_errors Off
```

这些需要 [补丁 0001](/zh/development/patches/) 才能通过 lsphp 生效。
