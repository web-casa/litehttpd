---
title: "WordPress"
description: "Running WordPress on OpenLiteSpeed with LiteSpeed Cache"
---

## Prerequisites

- OpenLiteSpeed installed and running
- PHP 8.1+ with required extensions (see [Install PHP](/ols/php-install/))
- MySQL or MariaDB server
- A configured virtual host (see [Virtual Hosts](/ols/virtual-hosts/))

## Database Setup

```bash
mysql -u root -p
```

```sql
CREATE DATABASE wordpress;
CREATE USER 'wpuser'@'localhost' IDENTIFIED BY 'strong_password_here';
GRANT ALL PRIVILEGES ON wordpress.* TO 'wpuser'@'localhost';
FLUSH PRIVILEGES;
EXIT;
```

## Install WordPress

### Using WP-CLI (Recommended)

```bash
# Install WP-CLI
curl -O https://raw.githubusercontent.com/wp-cli/builds/gh-pages/phar/wp-cli.phar
chmod +x wp-cli.phar
mv wp-cli.phar /usr/local/bin/wp

# Download WordPress
cd /var/www/example.com
wp core download --allow-root

# Configure
wp config create --allow-root \
  --dbname=wordpress \
  --dbuser=wpuser \
  --dbpass=strong_password_here \
  --dbhost=localhost

# Install
wp core install --allow-root \
  --url=https://example.com \
  --title="My Site" \
  --admin_user=admin \
  --admin_password=admin_password_here \
  --admin_email=admin@example.com

# Set ownership
chown -R nobody:nobody /var/www/example.com
```

### Manual Download

```bash
cd /var/www/example.com
wget https://wordpress.org/latest.tar.gz
tar xzf latest.tar.gz --strip-components=1
rm latest.tar.gz
chown -R nobody:nobody .
```

## Virtual Host Configuration

```apacheconf
virtualhost example {
  vhRoot                  /var/www/example.com
  configFile              conf/vhosts/example/vhconf.conf
  allowSymbolLink         1
  enableScript            1
  restrained              1

  docRoot                 $VH_ROOT/

  index {
    indexFiles             index.php, index.html
  }

  scripthandler {
    add                   lsapi:lsphp  php
  }

  # Enable .htaccess processing
  rewrite {
    enable                1
    autoLoadHtaccess      1
  }
}
```

## WordPress Permalink Rewrite Rules

OLS needs rewrite rules for WordPress pretty permalinks. There are two approaches:

### Option A: OLS Native Rewrite (in vhconf.conf)

```apacheconf
rewrite {
  enable                  1
  autoLoadHtaccess        1

  rules                   <<<END_rules
    RewriteEngine On
    RewriteBase /
    RewriteRule ^index\.php$ - [L]
    RewriteCond %{REQUEST_FILENAME} !-f
    RewriteCond %{REQUEST_FILENAME} !-d
    RewriteRule . /index.php [L]
  END_rules
}
```

### Option B: .htaccess (with LiteHTTPD)

WordPress automatically creates a `.htaccess` file. With the LiteHTTPD module loaded, OLS processes it natively:

```apacheconf
# WordPress default .htaccess
<IfModule mod_rewrite.c>
RewriteEngine On
RewriteBase /
RewriteRule ^index\.php$ - [L]
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule . /index.php [L]
</IfModule>
```

## LiteSpeed Cache Plugin

The LiteSpeed Cache plugin is the primary advantage of running WordPress on OLS. It provides server-level full-page caching without any additional configuration.

### Install the Plugin

```bash
wp plugin install litespeed-cache --activate --allow-root
```

Or install from the WordPress admin dashboard: Plugins > Add New > search "LiteSpeed Cache".

### Enable OLS Cache Module

In `httpd_config.conf`, ensure the cache module is loaded:

```apacheconf
module cache {
  ls_enabled              1

  checkPublicCache        1
  checkPrivateCache       1
  maxCacheObjSize         10000000
  maxStaleAge             200
  qsCache                 1
  reqCookieCache          1
  respCookieCache         1
  ignoreReqCacheCtrl      1
  ignoreRespCacheCtrl     0

  storagePath             /usr/local/lsws/cachedata
}
```

### Recommended Plugin Settings

After activation, go to LiteSpeed Cache > General:

- **Enable LiteSpeed Cache**: On
- **Guest Mode**: On (serves cached pages to first-time visitors)
- **Guest Optimization**: On

Under LiteSpeed Cache > Cache:

- **Cache Logged-in Users**: Off (for most sites)
- **Cache Commenters**: Off
- **Cache REST API**: On
- **Cache Mobile**: On (if using a responsive theme, use same cache as desktop)

### Cache Purge

```bash
# Purge all cache via WP-CLI
wp litespeed-purge all --allow-root

# Or via the admin bar in WordPress dashboard
```

## WordPress Multisite

### Subdirectory Multisite

The default `.htaccess` rewrite rules work for subdirectory multisite. WordPress generates:

```apacheconf
RewriteEngine On
RewriteBase /
RewriteRule ^index\.php$ - [L]

# Add a trailing slash to /wp-admin
RewriteRule ^([_0-9a-zA-Z-]+/)?wp-admin$ $1wp-admin/ [R=301,L]

RewriteCond %{REQUEST_FILENAME} -f [OR]
RewriteCond %{REQUEST_FILENAME} -d
RewriteRule ^ - [L]
RewriteRule ^([_0-9a-zA-Z-]+/)?(wp-(content|admin|includes).*) $2 [L]
RewriteRule ^([_0-9a-zA-Z-]+/)?(.*\.php)$ $2 [L]
RewriteRule . index.php [L]
```

### Subdomain Multisite

For subdomain multisite, configure a wildcard listener in OLS and use the same virtual host with wildcard ServerName:

```apacheconf
listener Wildcard {
  address                 *:443
  secure                  1
  map                     example     *.example.com, example.com
}
```

## Security Hardening

### Block PHP Execution in Uploads

With the LiteHTTPD module, add to `wp-content/uploads/.htaccess`:

```apacheconf
<FilesMatch "\.php$">
  Require all denied
</FilesMatch>
```

Or via OLS context in `vhconf.conf`:

```apacheconf
context /wp-content/uploads/ {
  allowBrowse             1
  location                $VH_ROOT/wp-content/uploads/

  phpIniOverride {
    php_admin_flag engine off
  }
}
```

### Restrict wp-login.php

Using `.htaccess` with the LiteHTTPD module:

```apacheconf
<Files wp-login.php>
  Require ip 192.168.1.0/24
</Files>
```

### Disable XML-RPC

```apacheconf
<Files xmlrpc.php>
  Require all denied
</Files>
```

## Performance Tuning

### Recommended php.ini for WordPress

```ini
memory_limit = 256M
upload_max_filesize = 64M
post_max_size = 64M
max_execution_time = 300
max_input_vars = 5000

; OPcache
opcache.enable = 1
opcache.memory_consumption = 256
opcache.max_accelerated_files = 10000
opcache.revalidate_freq = 60
```

### OLS Tuning for WordPress

```apacheconf
extprocessor lsphp {
  type                    lsapi
  ...
  env                     PHP_LSAPI_CHILDREN=20
  env                     LSAPI_MAX_REQS=5000
  env                     PHP_LSAPI_MAX_IDLE=300
  memSoftLimit            2047M
  memHardLimit            2047M
}
```

## Troubleshooting

**White screen of death:**
- Enable WP_DEBUG in `wp-config.php`: `define('WP_DEBUG', true);`
- Check `/usr/local/lsws/logs/error.log`

**Permalink 404 errors:**
- Verify `rewrite` is enabled in the virtual host
- Check that `autoLoadHtaccess` is `1` or that rewrite rules are in vhconf.conf
- Flush rewrite rules: Settings > Permalinks > Save Changes

**LiteSpeed Cache not working:**
- Verify the OLS cache module is enabled in `httpd_config.conf`
- Check the `X-LiteSpeed-Cache` response header (should be `hit` or `miss`)
- Ensure `storagePath` directory is writable

## Next Steps

- [LiteSpeed Cache](/ols/cache/) -- advanced cache configuration
- [Security Headers](/ols/security-headers/) -- add security headers via OLS or .htaccess
