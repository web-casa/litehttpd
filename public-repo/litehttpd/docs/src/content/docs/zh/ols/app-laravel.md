---
title: "Laravel"
description: "在 OpenLiteSpeed 上运行 Laravel 应用"
---

## 前置条件

- OpenLiteSpeed 已安装 PHP 8.2+（参见 [安装 PHP](/zh/ols/php-install/)）
- 所需 PHP 扩展：`mbstring`、`xml`、`bcmath`、`curl`、`zip`、`tokenizer`、`json`、`openssl`
- 已安装 Composer
- MySQL、MariaDB、PostgreSQL 或 SQLite

## 安装 Laravel

```bash
cd /var/www
composer create-project laravel/laravel example.com
cd example.com

# 设置权限
chown -R nobody:nobody storage bootstrap/cache
chmod -R 775 storage bootstrap/cache
```

## 配置 .env 文件

```bash
cp .env.example .env
php artisan key:generate
```

编辑 `.env`：

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

## 虚拟主机配置

Laravel 的入口文件是 `public/index.php`。OLS 的文档根目录必须指向 `public/` 目录：

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

## 重写规则

Laravel 需要将所有请求路由到 `index.php`。

### 方式 A：OLS 原生重写（在 vhconf.conf 中）

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

### 方式 B：.htaccess（使用 LiteHTTPD 模块）

Laravel 自带 `public/.htaccess` 文件，可与 LiteHTTPD 模块配合使用：

```apacheconf
<IfModule mod_rewrite.c>
    RewriteEngine On
    RewriteRule ^(.*)$ public/$1 [L]
</IfModule>
```

以及 `public/.htaccess` 中：

```apacheconf
<IfModule mod_rewrite.c>
    RewriteEngine On
    RewriteCond %{REQUEST_FILENAME} !-d
    RewriteCond %{REQUEST_FILENAME} !-f
    RewriteRule ^ index.php [L]
</IfModule>
```

## 静态资源上下文

直接提供静态资源，无需 PHP 处理：

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

创建存储符号链接：

```bash
php artisan storage:link
```

## Laravel PHP 设置

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

使用 LiteHTTPD 模块，还可以在 `.htaccess` 中设置按目录覆盖：

```apacheconf
php_value memory_limit 512M
php_value max_execution_time 600
```

## 生产环境优化

```bash
# 缓存配置
php artisan config:cache

# 缓存路由
php artisan route:cache

# 缓存视图
php artisan view:cache

# 优化自动加载
composer install --optimize-autoloader --no-dev
```

## 队列工作进程

对于 Laravel 队列，使用 systemd 管理工作进程：

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

## 任务调度

添加到 `nobody` 用户的 crontab：

```bash
* * * * * cd /var/www/example.com && /usr/local/lsws/lsphp84/bin/php artisan schedule:run >> /dev/null 2>&1
```

## 故障排除

**500 内部服务器错误：**
- 检查 `storage/logs/laravel.log`
- 确认 `storage/` 和 `bootstrap/cache/` 目录对 OLS 用户可写
- 如果配置缓存过期，运行 `php artisan config:clear`

**所有路由返回 404：**
- 确认重写规则已生效（在 vhconf.conf 或 .htaccess 中）
- 确保 `docRoot` 指向 `public/` 目录

**会话/缓存问题：**
- 如果使用 Redis 驱动，确认 Redis 正在运行
- 检查 `storage/framework/sessions/` 和 `storage/framework/cache/` 的文件权限
