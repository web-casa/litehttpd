---
title: WordPress Migration
description: Migrating WordPress from Apache to OpenLiteSpeed with LiteHTTPD
---

## WordPress .htaccess Compatibility

LiteHTTPD passes all 18 WordPress compatibility tests with 19 popular plugins installed:

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

## Standard WordPress .htaccess

The default WordPress `.htaccess` works without modifications:

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
RewriteRule execution requires [Patch 0002](/development/patches/). Without it, OLS processes rewrite rules via its native `RewriteFile` mechanism, which handles basic WordPress routing but may not support all flag combinations.
:::

## Plugin-Generated Rules

WordPress plugins typically add rules to `.htaccess` for:

| Plugin Type | Directives Used |
|------------|----------------|
| Security (AIOS, Wordfence) | `<FilesMatch>`, `Require`, `Order/Deny`, `<Files>` |
| Cache (W3TC, WPSC) | `ExpiresByType`, `Header set Cache-Control`, `AddType` |
| SEO (Yoast, Rank Math) | `Redirect`, `RewriteRule` |
| SSL (Really Simple SSL) | `RewriteCond %{HTTPS}`, `RewriteRule [R=301]` |
| Image (WebP Express) | `RewriteCond %{HTTP_ACCEPT}`, `AddType` |

All of these are fully supported by LiteHTTPD.

## PHP Configuration

For WordPress-specific PHP settings in `.htaccess`:

```apache
php_value upload_max_filesize 64M
php_value post_max_size 64M
php_value memory_limit 256M
php_flag display_errors Off
```

These require [Patch 0001](/development/patches/) to take effect via lsphp.
