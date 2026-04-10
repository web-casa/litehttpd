---
title: PHP Configuration
description: PHP INI directives via .htaccess
---

:::caution
PHP configuration directives require [Patch 0001](/development/patches/) to take effect. Without it, directives are parsed but not passed to lsphp.
:::

## Directives

| Directive | Syntax |
|-----------|--------|
| `php_value` | `php_value name value` |
| `php_flag` | `php_flag name On\|Off` |
| `php_admin_value` | `php_admin_value name value` |
| `php_admin_flag` | `php_admin_flag name On\|Off` |

`php_admin_*` variants cannot be overridden by user scripts.

## Examples

### WordPress Upload Limits

```apache
php_value upload_max_filesize 64M
php_value post_max_size 64M
php_value memory_limit 256M
php_value max_execution_time 300
```

### Security Settings

```apache
php_flag display_errors Off
php_flag log_errors On
php_value error_log /var/log/php_errors.log
php_flag expose_php Off
```

### Session Configuration

```apache
php_value session.save_path /tmp/sessions
php_value session.gc_maxlifetime 3600
php_flag session.use_strict_mode On
```
