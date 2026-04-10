---
title: "PHP Environment Variables"
description: "Tuning PHP worker processes and configuration for OpenLiteSpeed"
---

## LSAPI Environment Variables

These environment variables control how lsphp worker processes behave. Set them in the extprocessor configuration in `httpd_config.conf`:

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

### Variable Reference

| Variable | Default | Description |
|----------|---------|-------------|
| `PHP_LSAPI_CHILDREN` | 10 | Number of PHP worker processes. Each handles one request at a time. |
| `LSAPI_MAX_REQS` | 10000 | Requests a worker serves before exiting and being replaced. Lower values help prevent memory leaks. |
| `PHP_LSAPI_MAX_IDLE` | 300 | Seconds a worker can sit idle before being terminated. |
| `LSAPI_AVOID_FORK` | 0 | Set to `1` to use `exec()` instead of `fork()` for new workers. Useful on systems with overcommit disabled. |
| `LSAPI_ACCEPT_NOTIFY` | 1 | Notify parent process when a child accepts a connection. Enables better load balancing. |
| `LSAPI_PGRP_MAX_IDLE` | 300 | Seconds the entire process group can be idle before all workers exit. |
| `LSAPI_MAX_IDLE_CHILDREN` | PHP_LSAPI_CHILDREN / 3 | Maximum number of idle children to keep alive. |
| `LSAPI_MAX_PROCESS_TIME` | 300 | Maximum seconds a single request can take before the worker is killed. |

## Sizing PHP_LSAPI_CHILDREN

The number of children directly impacts concurrency and memory usage.

**Formula:**

```
PHP_LSAPI_CHILDREN = Available_Memory_MB / Per_Worker_MB
```

Typical per-worker memory usage:

| Application | Per-worker memory |
|-------------|------------------|
| Static PHP (phpinfo) | 20-30 MB |
| WordPress | 40-60 MB |
| Laravel | 50-80 MB |
| Magento | 80-120 MB |

**Example:** A server with 4 GB available for PHP running WordPress:

```
4096 MB / 60 MB = ~68 workers (conservative: 50)
```

Set `maxConns` in the extprocessor to match `PHP_LSAPI_CHILDREN`.

## php.ini Location

Each lsphp version has its own `php.ini`:

```
/usr/local/lsws/lsphp84/etc/php/8.4/litespeed/php.ini
```

If this file does not exist, lsphp falls back to:

```
/usr/local/lsws/lsphp84/etc/php.ini
```

To find the active configuration:

```bash
/usr/local/lsws/lsphp84/bin/lsphp -i | grep "Loaded Configuration File"
```

## Common php.ini Tuning

### Memory and Upload Limits

```ini
memory_limit = 256M
upload_max_filesize = 64M
post_max_size = 64M
max_execution_time = 300
max_input_time = 300
max_input_vars = 5000
```

### OPcache Settings

OPcache is critical for PHP performance. Enable and tune it:

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

For production servers where code does not change frequently:

```ini
opcache.validate_timestamps = 0
```

This disables file modification checks. You must restart lsphp after deploying new code.

### Session Configuration

```ini
session.save_handler = files
session.save_path = /tmp
session.gc_maxlifetime = 1440
```

For multi-server setups, use Redis or Memcached:

```ini
session.save_handler = redis
session.save_path = "tcp://127.0.0.1:6379"
```

### Error Logging

```ini
display_errors = Off
log_errors = On
error_log = /var/log/php/error.log
error_reporting = E_ALL & ~E_DEPRECATED & ~E_STRICT
```

## Per-Directory PHP Settings with LiteHTTPD

When the LiteHTTPD module is installed, you can override `php.ini` values per directory using `.htaccess`:

```apacheconf
php_value memory_limit 512M
php_value upload_max_filesize 128M
php_value post_max_size 128M
php_flag display_errors Off
```

:::note
`php_value` and `php_flag` directives in `.htaccess` require the LiteHTTPD module with OLS compiled with the PHPConfig patch. On stock OLS, these directives are parsed but not applied.
:::

## Applying Changes

After modifying environment variables in `httpd_config.conf`:

```bash
systemctl restart lsws
```

After modifying `php.ini`:

```bash
# Full restart
systemctl restart lsws

# Or gracefully restart just lsphp
killall -USR1 lsphp
```

## Monitoring PHP Workers

Check the current number of lsphp processes:

```bash
ps aux | grep lsphp | grep -v grep | wc -l
```

Monitor memory usage per worker:

```bash
ps -C lsphp -o pid,rss,vsz,pcpu,args --sort=-rss
```

## Next Steps

- [PHP LSAPI](/ols/php-lsapi/) -- detailed LSAPI architecture
- [Install PHP](/ols/php-install/) -- install additional PHP versions
