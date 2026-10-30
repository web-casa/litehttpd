---
title: Troubleshooting
description: Common LiteHTTPD installation and configuration problems
---

Start with these checks before changing application code:

```bash
grep -n "litehttpd_htaccess" /usr/local/lsws/conf/httpd_config.conf
grep -n "allowOverride\\|autoLoadHtaccess" /usr/local/lsws/conf/vhosts/*/vhconf.conf
/usr/local/lsws/bin/lswsctrl restart
tail -n 100 /usr/local/lsws/logs/error.log
```

## RPM Setup Rejects the Server

The RPM repository setup script supports EL 8/9/10 on x86_64. If it reports an unsupported architecture, use a source build or module install for that platform.

## `.htaccess` Changes Do Not Apply

Check the vhost config first:

```apacheconf
allowOverride 255
autoLoadHtaccess 0
```

Then verify the module is enabled in `httpd_config.conf`:

```apacheconf
module litehttpd_htaccess {
    ls_enabled              1
}
```

Restart OLS after changing either file. A graceful reload is not always enough when module or vhost settings changed.

## Directives Seem to Run Twice

If the vhost still has `autoLoadHtaccess 1`, OLS native `.htaccess` parsing and LiteHTTPD can both process overlapping directives such as `ErrorDocument` and `Options`. Change it to:

```apacheconf
autoLoadHtaccess 0
```

LiteHTTPD will still load `.htaccess` through its own hooks.

## RewriteRule Redirects Do Not Work

LiteHTTPD-Thin parses `RewriteRule`, but stock OLS does not execute LiteHTTPD's rewrite handles. Use LiteHTTPD-Full when plugins or applications depend on `.htaccess` rewrite execution, especially redirect rules with flags such as `[R=301,L,QSA,F,G]`.

## `php_value` or `php_flag` Does Not Apply

These directives require LiteHTTPD-Full. In Thin mode they are parsed for compatibility but are not passed to lsphp.

Also confirm the site actually uses lsphp/LSAPI. PHP-FPM or proxy-based PHP routing will not receive LSIAPI PHPConfig values.

## AddHandler or SetHandler Does Not Change PHP Handling

`AddHandler`, `SetHandler`, `RemoveHandler`, and `Action` are compatibility parses. OLS request handling is configured with `scriptHandler` and `extProcessor` blocks, not Apache per-directory handler remapping.

## A Site Starts Returning 403

LiteHTTPD makes previously ignored security rules active. Check for:

- `<Files>` or `<FilesMatch>` blocks with `Require all denied`
- `Require ip` or legacy `Order` / `Allow` / `Deny` rules
- `Options -Indexes`

This is often the expected result for protected files such as `wp-config.php`. If a normal route returns 403, inspect the nearest `.htaccess` files from the document root down to the requested path.

## WordPress Permalinks Are Slow

This is usually OLS PHP routing overhead, not `.htaccess` parsing. Use LSCache for cacheable pages and confirm static assets are served directly by OLS.
