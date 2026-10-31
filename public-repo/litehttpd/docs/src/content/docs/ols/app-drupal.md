---
title: "Drupal"
description: "Running Drupal on OpenLiteSpeed"
---

## Prerequisites

- OpenLiteSpeed with PHP 8.2+ (see [Install PHP](/ols/php-install/))
- Required PHP extensions: `gd`, `mbstring`, `xml`, `pdo_mysql`, `opcache`, `curl`
- MySQL 8.0+ or MariaDB 10.6+
- Composer

## Install Drupal

```bash
cd /var/www
composer create-project drupal/recommended-project example.com
cd example.com

# Set permissions
chown -R nobody:nobody .
chmod -R 755 web/sites/default
cp web/sites/default/default.settings.php web/sites/default/settings.php
chmod 644 web/sites/default/settings.php
mkdir -p web/sites/default/files
chmod 775 web/sites/default/files
```

## Database Setup

```bash
mysql -u root -p
```

```sql
CREATE DATABASE drupal CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER 'drupaluser'@'localhost' IDENTIFIED BY 'strong_password_here';
GRANT ALL PRIVILEGES ON drupal.* TO 'drupaluser'@'localhost';
FLUSH PRIVILEGES;
EXIT;
```

## Virtual Host Configuration

Drupal's web root is the `web/` subdirectory:

```apacheconf
virtualhost example {
  vhRoot                  /var/www/example.com
  configFile              conf/vhosts/example/vhconf.conf
  allowSymbolLink         1
  enableScript            1
  restrained              1

  docRoot                 $VH_ROOT/web/

  index {
    indexFiles             index.php, index.html
  }

  scripthandler {
    add                   lsapi:lsphp  php
  }

  rewrite {
    enable                1
    autoLoadHtaccess      1
  }
}
```

## Clean URL Rewrite Rules

Drupal requires clean URLs to function properly.

### OLS Native Rewrite (in vhconf.conf)

```apacheconf
rewrite {
  enable                  1
  rules                   <<<END_rules
    RewriteEngine On
    RewriteBase /
    RewriteCond %{REQUEST_FILENAME} !-f
    RewriteCond %{REQUEST_FILENAME} !-d
    RewriteCond %{REQUEST_URI} !=/favicon.ico
    RewriteRule ^ index.php [L]
  END_rules
}
```

### .htaccess (with LiteHTTPD)

Drupal ships a `.htaccess` file in its `web/` directory. With the LiteHTTPD module, it is processed automatically. The key rewrite section:

```apacheconf
<IfModule mod_rewrite.c>
  RewriteEngine on

  # Rewrite URLs of the form 'x' to the form 'index.php?q=x'.
  RewriteBase /
  RewriteCond %{REQUEST_FILENAME} !-f
  RewriteCond %{REQUEST_FILENAME} !-d
  RewriteCond %{REQUEST_URI} !=/favicon.ico
  RewriteRule ^ index.php [L]
</IfModule>
```

## Complete the Installation

Navigate to `https://example.com` in your browser and follow the Drupal installation wizard, or install via Drush:

```bash
cd /var/www/example.com

# Install Drush
composer require drush/drush

# Run installer
./vendor/bin/drush site:install standard \
  --db-url=mysql://drupaluser:strong_password_here@localhost/drupal \
  --site-name="My Drupal Site" \
  --account-name=admin \
  --account-pass=admin_password_here \
  -y
```

## Post-Installation Security

Lock down `settings.php`:

```bash
chmod 444 web/sites/default/settings.php
chmod 555 web/sites/default
```

### Block Direct Access to Sensitive Files

With the LiteHTTPD module, Drupal's `.htaccess` already blocks access to `.yml`, `.twig`, and other sensitive file types:

```apacheconf
<FilesMatch "\.(engine|inc|install|make|module|profile|po|sh|.*sql|theme|twig|tpl(\.php)?|xtmpl|yml)(~|\.sw[op]|\.bak|\.orig|\.save)?$|^(\.(?!well-known).*|Entries.*|Repository|Root|Tag|Template|composer\.(json|lock)|web\.config)$|^#.*#$|\.php(~|\.sw[op]|\.bak|\.orig|\.save)$">
  Require all denied
</FilesMatch>
```

Or via OLS context:

```apacheconf
context exp:^.+\.(yml|twig|engine|inc|install|module|profile|theme)$ {
  allowBrowse             0
}
```

## PHP Settings for Drupal

```ini
memory_limit = 256M
max_execution_time = 300
upload_max_filesize = 64M
post_max_size = 64M
max_input_vars = 5000

; OPcache
opcache.enable = 1
opcache.memory_consumption = 256
opcache.max_accelerated_files = 20000
```

## Cron

Drupal requires periodic cron runs:

```bash
# Add to crontab
*/15 * * * * cd /var/www/example.com && ./vendor/bin/drush cron >> /dev/null 2>&1
```

## Troubleshooting

**"Clean URLs" not working:**
- Verify rewrite rules are active
- Check that `mod_rewrite` is referenced in `.htaccess` and LiteHTTPD is loaded
- Run `drush status` to check clean URL status

**Permission errors:**
- Ensure `web/sites/default/files/` is writable by the OLS user (`nobody`)
- Check `settings.php` is readable

**Database connection errors:**
- Verify credentials in `settings.php`
- Check MySQL is running: `systemctl status mysql`
