---
title: "Virtual Hosts"
description: "Configure virtual hosts in OpenLiteSpeed with document roots, contexts, script handlers, and .htaccess support."
---

Each virtual host in OpenLiteSpeed has its own configuration file located under `/usr/local/lsws/conf/vhosts/<vhost-name>/vhconf.conf`.

## Virtual Host Configuration Structure

```
vhconf.conf
  docRoot
  index
  errorlog
  accesslog
  scriptHandler { }
  context(s) { }
  rewrite { }
  accessControl { }
  general { }
```

## Creating a Virtual Host

### Via WebAdmin

1. Navigate to **Virtual Hosts** in the left menu.
2. Click **Add** to create a new virtual host.
3. Fill in the name, document root, and config file path.
4. Save and restart.

### Via Config Files

Create the directory and config file:

```bash
mkdir -p /usr/local/lsws/conf/vhosts/example.com
```

Create `/usr/local/lsws/conf/vhosts/example.com/vhconf.conf`:

```apacheconf
docRoot                   /var/www/example.com/public/

index {
    useServer               0
    indexFiles               index.php, index.html
}

errorlog /usr/local/lsws/logs/example.com-error.log {
    useServer               0
    logLevel                WARN
    rollingSize             10M
}

accesslog /usr/local/lsws/logs/example.com-access.log {
    useServer               0
    logFormat               "%h %l %u %t \"%r\" %>s %b"
    rollingSize             10M
}
```

Then reference it in `httpd_config.conf`:

```apacheconf
virtualhost example.com {
    vhRoot                  /usr/local/lsws/conf/vhosts/example.com/
    configFile              $VH_ROOT/vhconf.conf
    allowSymbolLink         1
    enableScript            1
    restrained              1
}

listener Default {
    address                 *:80
    secure                  0
    map                     example.com example.com
}
```

## Document Root

```apacheconf
docRoot                   /var/www/example.com/public/
```

The `docRoot` can use the `$VH_ROOT` variable:

```apacheconf
docRoot                   $VH_ROOT/public/
```

## Script Handlers

Script handlers define how OLS processes dynamic content:

```apacheconf
scriptHandler {
    add                     lsapi:lsphp83 php
}
```

This routes all `.php` files through the lsphp83 LSAPI handler.

## Contexts

Contexts define per-directory or per-URI behavior:

```apacheconf
context / {
    location                $DOC_ROOT/
    allowBrowse             1
    indexFiles               index.php, index.html
}

context /protected/ {
    location                $DOC_ROOT/protected/
    allowBrowse             1
    realm                   SiteRealm
    authName                "Protected Area"
    required                valid-user
}

context /static/ {
    location                $DOC_ROOT/static/
    allowBrowse             1
    enableExpires           1
    expiresByType           image/*=A604800, text/css=A604800, application/javascript=A604800
}
```

## .htaccess Support with LiteHTTPD

OpenLiteSpeed has limited built-in .htaccess support. For full Apache-compatible .htaccess processing, use the LiteHTTPD module (`ols_htaccess.so`).

### Enable .htaccess Processing

In the virtual host config:

```apacheconf
general {
    allowOverride           All
    autoLoadHtaccess        1
    configFile              $VH_ROOT/vhconf.conf
}
```

- **allowOverride** -- Controls which .htaccess directive categories are permitted. Values: `All`, `None`, or a combination of `AuthConfig`, `FileInfo`, `Indexes`, `Limit`, `Options`.
- **autoLoadHtaccess** -- When set to `1`, OLS automatically loads and processes .htaccess files from the document root hierarchy.

With the LiteHTTPD module loaded, these settings enable full .htaccess support including rewrite rules, access control, MIME types, and more. See the [directive reference](/directives/) for the complete list of supported directives.

## Rewrite Rules

OLS supports rewrite rules in the virtual host config:

```apacheconf
rewrite {
    enable                  1
    rules                   <<<END_rules
RewriteEngine On
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule ^(.*)$ /index.php?$1 [L,QSA]
    END_rules
}
```

When using the LiteHTTPD module, rewrite rules can also be placed in `.htaccess` files.

## Access Control

Per-vhost IP access control:

```apacheconf
accessControl {
    allow                   ALL
    deny                    192.168.1.100
}
```

## Next Steps

- [Listeners](/ols/listeners/) -- bind the virtual host to a listener.
- [SSL / TLS](/ols/ssl/) -- enable HTTPS for the virtual host.
- [Custom Error Pages](/ols/custom-errors/) -- configure error documents.
- [Logs](/ols/logs/) -- per-vhost logging configuration.
