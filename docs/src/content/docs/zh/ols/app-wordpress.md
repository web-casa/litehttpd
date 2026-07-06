---
title: "WordPress"
description: "在 OpenLiteSpeed 上运行 WordPress 并使用 LiteSpeed Cache"
---

## 前置条件

- OpenLiteSpeed 已安装并运行
- PHP 8.1+ 及所需扩展（参见 [安装 PHP](/zh/ols/php-install/)）
- MySQL 或 MariaDB 服务器
- 已配置虚拟主机（参见 [虚拟主机](/zh/ols/virtual-hosts/)）

## 数据库设置

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

## 安装 WordPress

### 使用 WP-CLI（推荐）

```bash
# 安装 WP-CLI
curl -O https://raw.githubusercontent.com/wp-cli/builds/gh-pages/phar/wp-cli.phar
chmod +x wp-cli.phar
mv wp-cli.phar /usr/local/bin/wp

# 下载 WordPress
cd /var/www/example.com
wp core download --allow-root

# 配置
wp config create --allow-root \
  --dbname=wordpress \
  --dbuser=wpuser \
  --dbpass=strong_password_here \
  --dbhost=localhost

# 安装
wp core install --allow-root \
  --url=https://example.com \
  --title="My Site" \
  --admin_user=admin \
  --admin_password=admin_password_here \
  --admin_email=admin@example.com

# 设置文件所有权
chown -R nobody:nobody /var/www/example.com
```

### 手动下载

```bash
cd /var/www/example.com
wget https://wordpress.org/latest.tar.gz
tar xzf latest.tar.gz --strip-components=1
rm latest.tar.gz
chown -R nobody:nobody .
```

## 虚拟主机配置

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

  # 启用 .htaccess 处理
  rewrite {
    enable                1
    autoLoadHtaccess      1
  }
}
```

## WordPress 固定链接重写规则

OLS 需要重写规则来支持 WordPress 的友好固定链接。有两种方式：

### 方式 A：OLS 原生重写（在 vhconf.conf 中）

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

### 方式 B：.htaccess（使用 LiteHTTPD 模块）

WordPress 会自动创建 `.htaccess` 文件。加载 LiteHTTPD 模块后，OLS 可以原生处理该文件：

```apacheconf
# WordPress 默认 .htaccess
<IfModule mod_rewrite.c>
RewriteEngine On
RewriteBase /
RewriteRule ^index\.php$ - [L]
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule . /index.php [L]
</IfModule>
```

## LiteSpeed Cache 插件

LiteSpeed Cache 插件是在 OLS 上运行 WordPress 的核心优势。它提供服务器级全页面缓存，无需额外配置。

### 安装插件

```bash
wp plugin install litespeed-cache --activate --allow-root
```

或通过 WordPress 管理后台安装：插件 > 安装插件 > 搜索 "LiteSpeed Cache"。

### 启用 OLS 缓存模块

在 `httpd_config.conf` 中，确保缓存模块已加载：

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

### 推荐插件设置

激活后，进入 LiteSpeed Cache > 常规：

- **启用 LiteSpeed Cache**：开启
- **访客模式**：开启（为首次访问者提供缓存页面）
- **访客优化**：开启

在 LiteSpeed Cache > 缓存 下：

- **缓存已登录用户**：关闭（适用于大多数站点）
- **缓存评论者**：关闭
- **缓存 REST API**：开启
- **缓存移动端**：开启（如果使用响应式主题，可与桌面端共用缓存）

### 缓存清除

```bash
# 通过 WP-CLI 清除所有缓存
wp litespeed-purge all --allow-root

# 或通过 WordPress 后台管理栏清除
```

## WordPress 多站点

### 子目录多站点

默认的 `.htaccess` 重写规则适用于子目录多站点。WordPress 会生成：

```apacheconf
RewriteEngine On
RewriteBase /
RewriteRule ^index\.php$ - [L]

# 为 /wp-admin 添加尾部斜杠
RewriteRule ^([_0-9a-zA-Z-]+/)?wp-admin$ $1wp-admin/ [R=301,L]

RewriteCond %{REQUEST_FILENAME} -f [OR]
RewriteCond %{REQUEST_FILENAME} -d
RewriteRule ^ - [L]
RewriteRule ^([_0-9a-zA-Z-]+/)?(wp-(content|admin|includes).*) $2 [L]
RewriteRule ^([_0-9a-zA-Z-]+/)?(.*\.php)$ $2 [L]
RewriteRule . index.php [L]
```

### 子域名多站点

对于子域名多站点，需要在 OLS 中配置通配符监听器，并在同一虚拟主机中使用通配符 ServerName：

```apacheconf
listener Wildcard {
  address                 *:443
  secure                  1
  map                     example     *.example.com, example.com
}
```

## 安全加固

### 禁止上传目录执行 PHP

使用 LiteHTTPD 模块，在 `wp-content/uploads/.htaccess` 中添加：

```apacheconf
<FilesMatch "\.php$">
  Require all denied
</FilesMatch>
```

或通过 OLS 上下文在 `vhconf.conf` 中配置：

```apacheconf
context /wp-content/uploads/ {
  allowBrowse             1
  location                $VH_ROOT/wp-content/uploads/

  phpIniOverride {
    php_admin_flag engine off
  }
}
```

### 限制 wp-login.php 访问

使用 LiteHTTPD 模块在 `.htaccess` 中配置：

```apacheconf
<Files wp-login.php>
  Require ip 192.168.1.0/24
</Files>
```

### 禁用 XML-RPC

```apacheconf
<Files xmlrpc.php>
  Require all denied
</Files>
```

## 性能调优

### WordPress 推荐 php.ini 配置

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

### 针对 WordPress 的 OLS 调优

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

## 故障排除

**白屏（死亡白屏）：**
- 在 `wp-config.php` 中启用 WP_DEBUG：`define('WP_DEBUG', true);`
- 检查 `/usr/local/lsws/logs/error.log`

**固定链接 404 错误：**
- 确认虚拟主机中已启用 `rewrite`
- 检查 `autoLoadHtaccess` 是否为 `1`，或重写规则是否已写入 vhconf.conf
- 刷新重写规则：设置 > 固定链接 > 保存更改

**LiteSpeed Cache 不生效：**
- 确认 `httpd_config.conf` 中已启用 OLS 缓存模块
- 检查 `X-LiteSpeed-Cache` 响应头（应为 `hit` 或 `miss`）
- 确保 `storagePath` 目录可写

## 后续步骤

- [LiteSpeed Cache](/zh/ols/cache/) -- 高级缓存配置
- [安全响应头](/zh/ols/security-headers/) -- 通过 OLS 或 .htaccess 添加安全响应头
