---
title: From aaPanel (BT Panel)
description: Switch from aaPanel's OLS to LiteHTTPD
---

## Overview

aaPanel (宝塔面板) installs OLS via its App Store. It uses standard OLS paths (`/usr/local/lsws/`) but manages config files through the panel UI. Stock aaPanel OLS has **no .htaccess support** beyond basic `RewriteFile` processing — which is why many aaPanel + OLS users find that `.htaccess` rules don't work.

LiteHTTPD adds full `.htaccess` support (80 directives) to your aaPanel OLS setup.

## Installation

```bash
# Add LiteHTTPD repo and install
curl -s https://rpms.litehttpd.com/setup.sh | bash
dnf install openlitespeed-litehttpd

# Restart
systemctl restart lsws
```

For Thin mode (no binary replacement), see [From Stock OLS](/migration/ols-to-litehttpd/).

## aaPanel-Specific Considerations

### Do Not Upgrade OLS via App Store

The aaPanel App Store "Upgrade" button re-installs the stock OLS binary, overwriting the patched version. After any aaPanel-triggered OLS upgrade:

```bash
dnf reinstall openlitespeed-litehttpd
systemctl restart lsws
```

### vhost Config Regeneration

aaPanel regenerates vhost `.conf` files when you change site settings (PHP version, SSL, domain aliases). Custom directives added to `vhost.conf` may be lost.

**Use `.htaccess` files for per-site rules** — aaPanel does not modify files in your document root.

### Verify Module Config After Panel Operations

aaPanel modifies `httpd_config.conf` on server-level changes. The LiteHTTPD module block is usually preserved, but verify after major panel operations:

```bash
grep -c 'litehttpd_htaccess' /usr/local/lsws/conf/httpd_config.conf
# Should output: 1
```

### Disable OLS autoLoadHtaccess

If your aaPanel vhost configs have `autoLoadHtaccess 1`, disable it to avoid double-processing with LiteHTTPD:

```bash
grep -r 'autoLoadHtaccess' /usr/local/lsws/conf/vhosts/
# If any show 1, change to 0:
sed -i 's/autoLoadHtaccess.*1/autoLoadHtaccess 0/' /usr/local/lsws/conf/vhosts/*/vhost.conf
```

### PHP-FPM vs LSPHP

Some aaPanel versions install PHP-FPM instead of LSPHP for OLS. LiteHTTPD's `php_value`/`php_flag` directives (Full mode) require LSPHP (LSAPI protocol).

Check which PHP you're running:

```bash
# If this returns a path, you have LSPHP
ls /usr/local/lsws/lsphp*/bin/lsphp 2>/dev/null

# Check your vhost config for the handler type
grep -A5 'extProcessor' /usr/local/lsws/conf/vhosts/*/vhost.conf
```

If you see PHP-FPM instead of LSPHP:

```bash
dnf install lsphp83 lsphp83-common lsphp83-mysqlnd
```

Then update the external app path in the vhost config to `/usr/local/lsws/lsphp83/bin/lsphp`, or change it through the OLS WebAdmin console (port 7080).

### Enable .htaccess for Each Site

Stock aaPanel OLS may not have `.htaccess` processing enabled for your sites. After installing LiteHTTPD, the module automatically processes `.htaccess` files — no additional OLS-level configuration is needed. The module hooks into the request pipeline directly.

However, for WordPress permalink support, ensure rewrite is enabled in each vhost:

```
# In /usr/local/lsws/conf/vhosts/<name>/vhost.conf
rewrite {
  enable                  1
}
```

### Behavioral Changes

After installing LiteHTTPD, be aware of these changes from stock OLS behavior:

1. **`.htaccess` files now work** — This is the whole point, but review your `.htaccess` files. Directives that stock OLS previously ignored (like `Header set`, `Require all denied`, `FilesMatch`) are now active.

2. **`.ht*` files are blocked** — Requests to `.htaccess`, `.htpasswd` return 403 (Apache-compatible security). Stock OLS may serve or 404 these files.

3. **Path traversal blocked** — Encoded `../` sequences return 403.

## Verification

```bash
# Test .htaccess processing
echo 'Header set X-LiteHTTPD "active"' > /www/wwwroot/example.com/.htaccess
curl -sI https://example.com/ | grep X-LiteHTTPD
# Expected: X-LiteHTTPD: active

# Clean up
rm /www/wwwroot/example.com/.htaccess
```
