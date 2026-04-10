---
title: "Laravel"
description: "Running Laravel applications on OpenLiteSpeed"
---

## Prerequisites

- OpenLiteSpeed with PHP 8.2+ (see [Install PHP](/ols/php-install/))
- Required PHP extensions: `mbstring`, `xml`, `bcmath`, `curl`, `zip`, `tokenizer`, `json`, `openssl`
- Composer installed
- MySQL, MariaDB, PostgreSQL, or SQLite

## Install Laravel

```bash
cd /var/www
composer create-project laravel/laravel example.com
cd example.com

# Set permissions
chown -R nobody:nobody storage bootstrap/cache
chmod -R 775 storage bootstrap/cache
```

## Configure the .env File

```bash
cp .env.example .env
php artisan key:generate
```

Edit `.env`:

```ini
APP_NAME="My App"
APP_ENV=production
APP_DEBUG=false
APP_URL=https://example.com

DB_CONNECTION=mysql
DB_HOST=127.0.0.1
DB_PORT=3306
DB_DATABASE=laravel
DB_USERNAME=laraveluser
DB_PASSWORD=strong_password_here

CACHE_DRIVER=redis
SESSION_DRIVER=redis
QUEUE_CONNECTION=redis
```

## Virtual Host Configuration

Laravel's entry point is `public/index.php`. The OLS document root must point to the `public/` directory:

```apacheconf
virtualhost example {
  vhRoot                  /var/www/example.com
  configFile              conf/vhosts/example/vhconf.conf
  allowSymbolLink         1
  enableScript            1
  restrained              1

  docRoot                 $VH_ROOT/public/

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

## Rewrite Rules

Laravel requires all requests to be routed through `index.php`.

### Option A: OLS Native Rewrite (in vhconf.conf)

```apacheconf
rewrite {
  enable                  1
  rules                   <<<END_rules
    RewriteEngine On
    RewriteCond %{REQUEST_FILENAME} !-d
    RewriteCond %{REQUEST_FILENAME} !-f
    RewriteRule ^ index.php [L]
  END_rules
}
```

### Option B: .htaccess (with LiteHTTPD)

Laravel ships with a `public/.htaccess` file that works with the LiteHTTPD module:

```apacheconf
<IfModule mod_rewrite.c>
    RewriteEngine On
    RewriteRule ^(.*)$ public/$1 [L]
</IfModule>
```

And in `public/.htaccess`:

```apacheconf
<IfModule mod_rewrite.c>
    RewriteEngine On
    RewriteCond %{REQUEST_FILENAME} !-d
    RewriteCond %{REQUEST_FILENAME} !-f
    RewriteRule ^ index.php [L]
</IfModule>
```

## Context for Static Assets

Serve static assets directly without PHP processing:

```apacheconf
context /build/ {
  location                $VH_ROOT/public/build/
  allowBrowse             1

  extraHeaders {
    set                   Cache-Control "public, max-age=31536000, immutable"
  }
}

context /storage/ {
  location                $VH_ROOT/storage/app/public/
  allowBrowse             1
}
```

Create the storage symlink:

```bash
php artisan storage:link
```

## PHP Settings for Laravel

```ini
memory_limit = 256M
max_execution_time = 300
upload_max_filesize = 64M
post_max_size = 64M

; OPcache
opcache.enable = 1
opcache.memory_consumption = 256
opcache.max_accelerated_files = 20000
opcache.validate_timestamps = 0
```

With the LiteHTTPD module, you can also set per-directory overrides in `.htaccess`:

```apacheconf
php_value memory_limit 512M
php_value max_execution_time 600
```

## Production Optimization

```bash
# Cache configuration
php artisan config:cache

# Cache routes
php artisan route:cache

# Cache views
php artisan view:cache

# Optimize autoloader
composer install --optimize-autoloader --no-dev
```

## Queue Workers

For Laravel queues, use systemd to manage the worker:

```ini
# /etc/systemd/system/laravel-worker.service
[Unit]
Description=Laravel Queue Worker
After=network.target

[Service]
User=nobody
Group=nobody
WorkingDirectory=/var/www/example.com
ExecStart=/usr/local/lsws/lsphp84/bin/php artisan queue:work redis --sleep=3 --tries=3 --max-time=3600
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
systemctl enable --now laravel-worker
```

## Task Scheduling

Add to the `nobody` user's crontab:

```bash
* * * * * cd /var/www/example.com && /usr/local/lsws/lsphp84/bin/php artisan schedule:run >> /dev/null 2>&1
```

## Troubleshooting

**500 Internal Server Error:**
- Check `storage/logs/laravel.log`
- Verify `storage/` and `bootstrap/cache/` are writable by the OLS user
- Run `php artisan config:clear` if configuration cache is stale

**404 on all routes:**
- Verify rewrite rules are active (either in vhconf.conf or .htaccess)
- Ensure `docRoot` points to the `public/` directory

**Session/cache issues:**
- Verify Redis is running if using Redis driver
- Check file permissions on `storage/framework/sessions/` and `storage/framework/cache/`
