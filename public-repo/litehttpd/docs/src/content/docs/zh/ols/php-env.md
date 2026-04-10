---
title: "PHP 环境变量"
description: "为 OpenLiteSpeed 调优 PHP 工作进程和配置。"
---

## LSAPI 环境变量

这些环境变量控制 lsphp 工作进程的行为。在 `httpd_config.conf` 的 extprocessor 配置中设置：

```apacheconf
extprocessor lsphp {
  type                    lsapi
  ...
  env                     PHP_LSAPI_CHILDREN=10
  env                     LSAPI_MAX_REQS=5000
  env                     PHP_LSAPI_MAX_IDLE=300
  env                     LSAPI_AVOID_FORK=0
  env                     LSAPI_ACCEPT_NOTIFY=1
}
```

### 变量参考

| 变量 | 默认值 | 说明 |
|----------|---------|-------------|
| `PHP_LSAPI_CHILDREN` | 10 | PHP 工作进程数。每个进程一次处理一个请求。 |
| `LSAPI_MAX_REQS` | 10000 | 工作进程在退出并被替换前处理的请求数。较低的值有助于防止内存泄漏。 |
| `PHP_LSAPI_MAX_IDLE` | 300 | 工作进程空闲多少秒后被终止。 |
| `LSAPI_AVOID_FORK` | 0 | 设为 `1` 使用 `exec()` 代替 `fork()` 创建新工作进程。在禁用 overcommit 的系统上有用。 |
| `LSAPI_ACCEPT_NOTIFY` | 1 | 子进程接受连接时通知父进程。启用更好的负载均衡。 |
| `LSAPI_PGRP_MAX_IDLE` | 300 | 整个进程组空闲多少秒后所有工作进程退出。 |
| `LSAPI_MAX_IDLE_CHILDREN` | PHP_LSAPI_CHILDREN / 3 | 保持活动的最大空闲子进程数。 |
| `LSAPI_MAX_PROCESS_TIME` | 300 | 单个请求的最大处理秒数，超过后工作进程将被终止。 |

## PHP_LSAPI_CHILDREN 的大小调整

子进程数直接影响并发能力和内存使用。

**计算公式：**

```
PHP_LSAPI_CHILDREN = 可用内存_MB / 每工作进程_MB
```

典型的每工作进程内存使用：

| 应用 | 每工作进程内存 |
|-------------|------------------|
| 静态 PHP（phpinfo） | 20-30 MB |
| WordPress | 40-60 MB |
| Laravel | 50-80 MB |
| Magento | 80-120 MB |

**示例：** 一台有 4 GB 可用内存运行 WordPress 的服务器：

```
4096 MB / 60 MB = ~68 个工作进程（保守值：50）
```

将 extprocessor 中的 `maxConns` 设为与 `PHP_LSAPI_CHILDREN` 一致。

## php.ini 位置

每个 lsphp 版本有自己的 `php.ini`：

```
/usr/local/lsws/lsphp84/etc/php/8.4/litespeed/php.ini
```

如果此文件不存在，lsphp 会回退到：

```
/usr/local/lsws/lsphp84/etc/php.ini
```

查找当前活动的配置：

```bash
/usr/local/lsws/lsphp84/bin/lsphp -i | grep "Loaded Configuration File"
```

## 常用 php.ini 调优

### 内存和上传限制

```ini
memory_limit = 256M
upload_max_filesize = 64M
post_max_size = 64M
max_execution_time = 300
max_input_time = 300
max_input_vars = 5000
```

### OPcache 设置

OPcache 对 PHP 性能至关重要。启用并调优它：

```ini
opcache.enable = 1
opcache.memory_consumption = 256
opcache.interned_strings_buffer = 16
opcache.max_accelerated_files = 10000
opcache.revalidate_freq = 60
opcache.validate_timestamps = 1
opcache.save_comments = 1
opcache.fast_shutdown = 1
```

对于代码不经常变更的生产服务器：

```ini
opcache.validate_timestamps = 0
```

这会禁用文件修改检查。部署新代码后必须重启 lsphp。

### 会话配置

```ini
session.save_handler = files
session.save_path = /tmp
session.gc_maxlifetime = 1440
```

对于多服务器环境，使用 Redis 或 Memcached：

```ini
session.save_handler = redis
session.save_path = "tcp://127.0.0.1:6379"
```

### 错误日志

```ini
display_errors = Off
log_errors = On
error_log = /var/log/php/error.log
error_reporting = E_ALL & ~E_DEPRECATED & ~E_STRICT
```

## 使用 LiteHTTPD 按目录覆盖 PHP 设置

安装 LiteHTTPD 模块后，您可以在 `.htaccess` 中按目录覆盖 `php.ini` 的值：

```apacheconf
php_value memory_limit 512M
php_value upload_max_filesize 128M
php_value post_max_size 128M
php_flag display_errors Off
```

:::note
`.htaccess` 中的 `php_value` 和 `php_flag` 指令需要 LiteHTTPD 模块，且 OLS 需要编译了 PHPConfig 补丁。在原版 OLS 上，这些指令会被解析但不会生效。
:::

## 应用更改

修改 `httpd_config.conf` 中的环境变量后：

```bash
systemctl restart lsws
```

修改 `php.ini` 后：

```bash
# 完全重启
systemctl restart lsws

# 或仅平滑重启 lsphp
killall -USR1 lsphp
```

## 监控 PHP 工作进程

查看当前 lsphp 进程数：

```bash
ps aux | grep lsphp | grep -v grep | wc -l
```

监控每个工作进程的内存使用：

```bash
ps -C lsphp -o pid,rss,vsz,pcpu,args --sort=-rss
```

## 下一步

- [PHP LSAPI](/zh/ols/php-lsapi/) -- 详细的 LSAPI 架构
- [安装 PHP](/zh/ols/php-install/) -- 安装其他 PHP 版本
