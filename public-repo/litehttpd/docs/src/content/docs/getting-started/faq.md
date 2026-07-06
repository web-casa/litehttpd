---
title: FAQ
description: Frequently asked questions about LiteHTTPD
---

## General

### Does LiteHTTPD require an OLS restart to apply .htaccess changes?

**No.** The module checks the `.htaccess` file's modification time on every request. When you edit a `.htaccess` file, the changes take effect on the very next request — no restart needed.

This applies to all directives: Header, Require, Redirect, RewriteRule, php_value, ExpiresActive, ErrorDocument, etc.

The only exception is **OLS vhost-level rewrite rules** (in `vhconf.conf`), which do require a graceful restart (`lswsctrl restart`). This is needed for WordPress permalink fallback routing — see [Troubleshooting](/getting-started/configuration/#wordpress-permalinks-return-404).

### Can I use different .htaccess files in different directories?

**Yes.** This is the core design of `.htaccess` — directory-level cascading. The module walks the directory tree from the document root to the requested file's directory, merging rules at each level. Child directories override parent directories.

Example:

```
/var/www/html/.htaccess              ← site-wide rules
/var/www/html/admin/.htaccess        ← admin area rules (override site-wide)
/var/www/html/admin/api/.htaccess    ← API rules (override admin + site-wide)
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

Result:
- `/index.html` → X-Frame-Options + caching (site-wide rules only)
- `/admin/dashboard.php` → **403 Forbidden** (denied by admin .htaccess)
- `/admin/api/data.json` from `10.0.0.1` → **200 OK** + 512M memory limit
- `/admin/api/data.json` from external IP → **403 Forbidden**

Each directory's `.htaccess` takes effect independently. Changes are instant — no restart required.

---

## WordPress

### Why do my WordPress permalinks return 404?

OLS defaults to `rewrite { enable 0 }` in the vhost config. The `.htaccess` RewriteRule is parsed by LiteHTTPD, but OLS's rewrite engine must be enabled at the vhost level.

**Fix:**

1. Edit your vhost config (e.g., `/usr/local/lsws/conf/vhosts/Example/vhconf.conf`):

```
rewrite {
  enable                  1
}
```

2. Also add a fallback rewrite rule in the vhost context (OLS returns 404 for non-existent paths before the module hook fires):

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

3. Restart OLS: `/usr/local/lsws/bin/lswsctrl restart`

> If you installed via the RPM repository (`dnf install openlitespeed-litehttpd`), steps 1 and the `index.php` addition are done automatically.

### Do WordPress plugins that depend on .htaccess work?

**Yes.** We tested 10 popular .htaccess-dependent plugins and all produce identical results to Apache:

| Plugin | Status |
|--------|--------|
| Wordfence Security | ✅ Works |
| All In One WP Security | ✅ Works |
| W3 Total Cache | ✅ Works |
| WP Super Cache | ✅ Works |
| Yoast SEO | ✅ Works |
| Redirection | ✅ Works |
| WPS Hide Login | ✅ Works |
| Disable XML-RPC | ✅ Works |
| iThemes Security | ✅ Works |
| HTTP Headers | ✅ Works |

### Can I use php_value and php_flag in .htaccess?

**Yes, in Full mode** (patched OLS). The values are passed to lsphp via the native PHPConfig LSIAPI.

```apache
php_value upload_max_filesize 64M
php_value post_max_size 64M
php_value max_execution_time 300
php_flag display_errors Off
```

In Thin mode (stock OLS), `php_value`/`php_flag` are parsed but not executed — the values are set as environment variables (`PHP_VALUE_*`) as a hint, but lsphp won't read them without the PHPConfig patch.

> **Note:** `php_admin_value` and `php_admin_flag` are **blocked** in `.htaccess` for security reasons. These could override critical settings like `disable_functions` and `open_basedir`. Use the OLS admin panel or `php.ini` for admin-level PHP settings.

---

## Performance

### How does LiteHTTPD compare to Apache in performance?

On a Linode 4C/8G VPS with WordPress and PHP 8.3:

| Metric | Apache 2.4 | OLS + LiteHTTPD |
|--------|-----------|-----------------|
| Static file RPS | 23,909 | **58,891** (2.5x faster) |
| PHP RPS (wp-login) | 274 | **292** (1.07x faster) |
| Server RSS | 618 MB | **449 MB** (27% less) |
| .htaccess compatibility | 10/10 | **10/10** |

### Does .htaccess processing slow down requests?

The overhead is minimal — less than 0.5ms per request. The module uses:
- **File stat caching** — only re-reads `.htaccess` when mtime changes
- **Parsed directive caching** — parsed directives are cached per-request
- **SHM-based brute force counters** — shared memory for cross-process state

### Why is WordPress permalink rewrite slower on OLS than Apache?

This is an OLS architectural limitation, not a LiteHTTPD issue. OLS's native rewrite engine + PHP routing has higher overhead than Apache's `mod_rewrite`. All OLS engines (stock, Thin, Full) show the same behavior (~1-4 rps for permalink routes vs Apache's ~70 rps).

For most real-world usage, this is not noticeable because:
- Page caching (LSCache plugin) eliminates PHP execution for cached pages
- Static assets (CSS/JS/images) are served at 58K+ RPS with no rewrite overhead
- The bottleneck is typically database queries, not URL routing

---

## Security

### Is my wp-config.php protected?

**Yes, if you have the right .htaccess rules.** Add this to your WordPress `.htaccess`:

```apache
<Files "wp-config.php">
Require all denied
</Files>
```

This blocks direct access (returns 403). Without LiteHTTPD, stock OLS **does not** process `<Files>` directives — leaving `wp-config.php` accessible (returns 200 with database credentials).

### Does LiteHTTPD protect against brute force attacks?

**Yes.** 8 built-in directives for progressive throttling:

```apache
LSBruteForceProtection On
LSBruteForceAllowedAttempts 5
LSBruteForceWindow 300
LSBruteForceAction throttle
```

Features include IP detection through proxies (`LSBruteForceXForwardedFor`), CIDR whitelist (`LSBruteForceWhitelist`), and custom path protection (`LSBruteForceProtectPath`).

---

## Installation

### What's the difference between Full and Thin mode?

| | Full Mode | Thin Mode |
|-|-----------|-----------|
| **OLS binary** | Patched (4 patches) | Stock (unmodified) |
| **Directives** | 80 (all features) | 70+ (no RewriteRule exec, no php_value) |
| **Install** | `dnf install openlitespeed-litehttpd` | Copy `.so` file |
| **Best for** | Production, full Apache migration | Quick evaluation, Docker |

### Can I install LiteHTTPD alongside stock OLS?

**Full mode** replaces the OLS binary with a patched version. It's a drop-in replacement — all existing OLS features continue to work. The patched binary is fully compatible with the standard OLS configuration.

**Thin mode** doesn't modify OLS at all — it's just a `.so` module loaded via `httpd_config.conf`.

### Which PHP should I install?

LiteHTTPD doesn't bundle PHP. You can use:

- **lsphp from LiteSpeed repo** — `dnf install lsphp83` (recommended for OLS)
- **php-litespeed from Remi** — `dnf install php-litespeed` (if you prefer Remi packages)
- **Any LSAPI-compatible PHP** — configure the `extProcessor` path in `httpd_config.conf`

### How do I switch from CyberPanel / aaPanel / Docker to LiteHTTPD?

See the Migration section for detailed instructions:
- [From Stock OLS](/migration/ols-to-litehttpd/)
- [From CyberPanel](/migration/cyberpanel/)
- [From aaPanel](/migration/aapanel/)
- [Docker Environments](/migration/docker/)
