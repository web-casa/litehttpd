---
title: WordPress Hosting
description: Full .htaccess support for WordPress on OpenLiteSpeed
---

## The Problem

WordPress plugins generate complex `.htaccess` rules for security, caching, SEO, and image optimization. Stock OpenLiteSpeed ignores most of these directives, leaving your site unprotected and misconfigured.

:::note
Full WordPress .htaccess compatibility -- including RewriteRule execution and php_value/php_flag -- requires LiteHTTPD (Full edition, installed via `openlitespeed-litehttpd`). LiteHTTPD-Thin handles security headers, caching, and ACL directives but RewriteRule redirects and PHP INI overrides require the Full edition with OLS patches applied.
:::

## Tested with 15 Popular Plugins

LiteHTTPD has been tested with the most popular WordPress plugins that write `.htaccess` rules:

| Category | Plugins | .htaccess Directives Used |
|----------|---------|--------------------------|
| Security | All-In-One Security, Wordfence | FilesMatch, Require, Order/Deny, Files |
| Cache | W3 Total Cache, WP Super Cache, Far Future Expiry | ExpiresByType, Header set Cache-Control, AddType |
| SEO | Yoast SEO, Rank Math | Redirect, RewriteRule |
| SSL | Really Simple SSL | RewriteCond %{HTTPS}, RewriteRule [R=301] |
| Image | WebP Express, EWWW, Imagify | RewriteCond %{HTTP_ACCEPT}, AddType |
| Headers | HTTP Headers | Header set (CSP, X-Frame-Options) |
| Cache (OLS) | LiteSpeed Cache | CacheLookup, RewriteRule |
| Hide | WP Hide & Security Enhancer | RewriteRule path obfuscation |

All 15 plugins pass the compatibility test with identical behavior to Apache httpd.

## What Works Out of the Box

- WordPress permalink rewrite rules
- Security plugin IP blocking and file protection
- Cache plugin Expires and Cache-Control headers
- SEO plugin redirects and sitemap rules
- SSL/HTTPS forced redirect
- Custom error pages (ErrorDocument)
- PHP configuration (php_value, php_flag)
- Directory index settings
- File upload restrictions

## Quick Setup

```bash
# Install LiteHTTPD (Full edition) via RPM
dnf install ./openlitespeed-litehttpd-*.rpm

# Or manually install the module
cp litehttpd_htaccess.so /usr/local/lsws/modules/

# Enable in OLS config
echo 'module litehttpd_htaccess { ls_enabled 1 }' >> /usr/local/lsws/conf/httpd_config.conf

# Enable .htaccess for WordPress vhost
echo -e "allowOverride 255\nautoLoadHtaccess 1" >> /usr/local/lsws/conf/vhosts/wordpress/vhconf.conf

# Restart
/usr/local/lsws/bin/lswsctrl restart
```

## Performance vs Apache

Running WordPress 6.9 with all 15 plugins active (217-line .htaccess):

| Metric | Apache httpd | LiteHTTPD |
|--------|-------------|-----------|
| Static file RPS | 10,618 | **21,960** (2.1x faster) |
| .htaccess parsing overhead | -4.2% | **-0.7%** |
| Baseline memory | 969 MB | **676 MB** (30% less) |
| .htaccess compatibility | 100% | 90%+ |
