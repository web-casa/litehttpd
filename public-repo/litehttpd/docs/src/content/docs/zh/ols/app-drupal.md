---
title: "Drupal"
description: "在 OpenLiteSpeed 上运行 Drupal"
---

## 前置条件

- OpenLiteSpeed 已安装 PHP 8.2+（参见 [安装 PHP](/zh/ols/php-install/)）
- 所需 PHP 扩展：`gd`、`mbstring`、`xml`、`pdo_mysql`、`opcache`、`curl`
- MySQL 8.0+ 或 MariaDB 10.6+
- Composer

## 安装 Drupal

```bash
cd /var/www
composer create-project drupal/recommended-project example.com
cd example.com

# 设置权限
chown -R nobody:nobody .
chmod -R 755 web/sites/default
cp web/sites/default/default.settings.php web/sites/default/settings.php
chmod 644 web/sites/default/settings.php
mkdir -p web/sites/default/files
chmod 775 web/sites/default/files
```

## 数据库设置

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

## 虚拟主机配置

Drupal 的 Web 根目录是 `web/` 子目录：

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

## 简洁 URL 重写规则

Drupal 需要简洁 URL 才能正常运行。

### OLS 原生重写（在 vhconf.conf 中）

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

### .htaccess（使用 LiteHTTPD 模块）

Drupal 在其 `web/` 目录中自带 `.htaccess` 文件。使用 LiteHTTPD 模块后，该文件会被自动处理。关键重写部分：

```apacheconf
<IfModule mod_rewrite.c>
  RewriteEngine on

  # 将 'x' 形式的 URL 重写为 'index.php?q=x' 形式。
  RewriteBase /
  RewriteCond %{REQUEST_FILENAME} !-f
  RewriteCond %{REQUEST_FILENAME} !-d
  RewriteCond %{REQUEST_URI} !=/favicon.ico
  RewriteRule ^ index.php [L]
</IfModule>
```

## 完成安装

在浏览器中访问 `https://example.com`，按照 Drupal 安装向导操作，或通过 Drush 安装：

```bash
cd /var/www/example.com

# 安装 Drush
composer require drush/drush

# 运行安装程序
./vendor/bin/drush site:install standard \
  --db-url=mysql://drupaluser:strong_password_here@localhost/drupal \
  --site-name="My Drupal Site" \
  --account-name=admin \
  --account-pass=admin_password_here \
  -y
```

## 安装后安全设置

锁定 `settings.php`：

```bash
chmod 444 web/sites/default/settings.php
chmod 555 web/sites/default
```

### 阻止直接访问敏感文件

使用 LiteHTTPD 模块后，Drupal 的 `.htaccess` 已经阻止了对 `.yml`、`.twig` 和其他敏感文件类型的访问：

```apacheconf
<FilesMatch "\.(engine|inc|install|make|module|profile|po|sh|.*sql|theme|twig|tpl(\.php)?|xtmpl|yml)(~|\.sw[op]|\.bak|\.orig|\.save)?$|^(\.(?!well-known).*|Entries.*|Repository|Root|Tag|Template|composer\.(json|lock)|web\.config)$|^#.*#$|\.php(~|\.sw[op]|\.bak|\.orig|\.save)$">
  Require all denied
</FilesMatch>
```

或通过 OLS 上下文配置：

```apacheconf
context exp:^.+\.(yml|twig|engine|inc|install|module|profile|theme)$ {
  allowBrowse             0
}
```

## Drupal PHP 设置

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

## 定时任务

Drupal 需要定期运行 cron：

```bash
# 添加到 crontab
*/15 * * * * cd /var/www/example.com && ./vendor/bin/drush cron >> /dev/null 2>&1
```

## 故障排除

**"简洁 URL" 不工作：**
- 确认重写规则已生效
- 检查 `.htaccess` 中引用了 `mod_rewrite` 且 LiteHTTPD 已加载
- 运行 `drush status` 检查简洁 URL 状态

**权限错误：**
- 确保 `web/sites/default/files/` 目录对 OLS 用户（`nobody`）可写
- 检查 `settings.php` 是否可读

**数据库连接错误：**
- 验证 `settings.php` 中的凭据
- 检查 MySQL 是否运行：`systemctl status mysql`
