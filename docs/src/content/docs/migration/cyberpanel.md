---
title: From CyberPanel
description: Switch from CyberPanel's OLS to LiteHTTPD
---

## Overview

CyberPanel uses OpenLiteSpeed as its web server. Depending on your setup, you may be running:

- **Stock OLS** (most users) — CyberPanel installed via the official LiteSpeed repo, no paid .htaccess module
- **CyberPanel OLS with htaccess module** (paid license) — CyberPanel's `cyberpanel_ols.so` module providing ~29 .htaccess directives

LiteHTTPD works with both scenarios. It provides 80 directives (vs CyberPanel's 29) and is free (GPLv3).

## Before You Start

Check which setup you have:

```bash
# Check if CyberPanel's htaccess module is installed
ls -la /usr/local/lsws/modules/cyberpanel_ols.so 2>/dev/null && echo "CyberPanel module found" || echo "No CyberPanel module"

# Check if the module is enabled in config
grep -q 'cyberpanel_ols' /usr/local/lsws/conf/httpd_config.conf && echo "Module enabled" || echo "Module not enabled"
```

## If You Have CyberPanel's Paid Module

If `cyberpanel_ols.so` is installed and enabled, disable it first to avoid conflicts. Both modules process the same `.htaccess` directives (`Header`, `ErrorDocument`, `php_value`, etc.) — loading both causes unpredictable behavior.

```bash
# Remove the CyberPanel module block from config
sed -i '/module cyberpanel_ols/,/}/d' /usr/local/lsws/conf/httpd_config.conf
```

### Feature Comparison

| Feature | CyberPanel .htaccess | LiteHTTPD |
|---------|---------------------|-----------|
| Directives | ~29 | 80 |
| RewriteRule execution | No | Yes (Full mode) |
| If/ElseIf/Else | No | Yes |
| ap_expr engine | No | Yes |
| Require directives | No | Yes |
| AuthType Basic | No | Yes |
| Options / AllowOverride | No | Yes |
| php_value to lsphp | Partial (PHP_INI_ALL only) | Full |
| License | $59/year or $199 lifetime | Free (GPLv3) |

## If You Have Stock OLS (No Paid Module)

Most CyberPanel users fall into this category. Your OLS has no `.htaccess` support beyond what OLS natively handles (basically just `RewriteFile` for basic rewrite rules). You can install LiteHTTPD directly.

## Installation

### Full Mode (Recommended)

```bash
# Add LiteHTTPD repo and install
curl -s https://rpms.litehttpd.com/setup.sh | bash
dnf install openlitespeed-litehttpd

# Restart
systemctl restart lsws
```

### Thin Mode (Keep Stock Binary)

If you prefer not to replace the OLS binary:

```bash
cp litehttpd_htaccess.so /usr/local/lsws/modules/

cat >> /usr/local/lsws/conf/httpd_config.conf <<'EOF'

module litehttpd_htaccess {
    ls_enabled              1
}
EOF

systemctl restart lsws
```

See [Editions](/getting-started/editions/) for the differences between Full and Thin.

## CyberPanel-Specific Considerations

### CyberPanel Upgrade Overwrites OLS Binary

This is the most important caveat. Running `cyberpanel_upgrade` or clicking "Upgrade" in the CyberPanel UI downloads a fresh OLS binary from `cyberpanel.net` and replaces whatever is at `/usr/local/lsws/bin/openlitespeed`.

After any CyberPanel upgrade:

```bash
dnf reinstall openlitespeed-litehttpd
systemctl restart lsws
```

### vhost.conf Is Auto-Regenerated

CyberPanel **fully rewrites** `/usr/local/lsws/conf/vhosts/{domain}/vhost.conf` when you:
- Issue or renew an SSL certificate
- Change PHP version
- Modify domain aliases
- Change any site-level setting in the panel

**Do not** add LiteHTTPD-specific config to `vhost.conf`. Use `.htaccess` files instead — CyberPanel never touches `.htaccess` files in your document root.

### httpd_config.conf Is Safer

CyberPanel modifies `httpd_config.conf` via surgical regex edits (not full regeneration). The LiteHTTPD module block will survive panel operations. However, verify after major CyberPanel upgrades:

```bash
grep -c 'litehttpd_htaccess' /usr/local/lsws/conf/httpd_config.conf
# Should output: 1
```

### Disable OLS autoLoadHtaccess

CyberPanel vhost templates may include `autoLoadHtaccess 1`, which enables OLS's native (limited) `.htaccess` parsing. With LiteHTTPD loaded, this creates double-processing for directives that both OLS and LiteHTTPD understand (like `ErrorDocument` and `Options`).

Check and disable:

```bash
# Check all vhost configs
grep -r 'autoLoadHtaccess' /usr/local/lsws/conf/vhosts/

# If any show autoLoadHtaccess 1, change to 0:
sed -i 's/autoLoadHtaccess.*1/autoLoadHtaccess 0/' /usr/local/lsws/conf/vhosts/*/vhost.conf
```

> **Note:** After CyberPanel regenerates a vhost.conf (e.g., on SSL renewal), `autoLoadHtaccess` may be re-enabled. Check periodically or use `.htaccess` files exclusively for site-level config.

### Behavioral Differences from Stock OLS

After installing LiteHTTPD, your server behaves slightly differently:

1. **`.ht*` files are blocked** — Requests to `.htaccess`, `.htpasswd`, etc. return 403 (Apache-compatible). Stock OLS either serves these files or returns 404. This is a security improvement.

2. **Path traversal returns 403** — Encoded `../` sequences (like `%2e%2e/`) return 403 instead of stock OLS's 400/404. This is a security improvement.

3. **All 80 .htaccess directives are processed** — If your `.htaccess` files contain directives that stock OLS previously ignored (like `Header set`, `Require`, `FilesMatch`), they now take effect. Review your `.htaccess` files to ensure no unintended rules become active.

## Verification

```bash
# Test .htaccess processing
echo 'Header set X-LiteHTTPD "active"' > /home/example.com/public_html/.htaccess
curl -sI https://example.com/ | grep X-LiteHTTPD
# Expected: X-LiteHTTPD: active

# Clean up
rm /home/example.com/public_html/.htaccess
```
