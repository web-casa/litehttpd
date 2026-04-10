---
title: PHP 配置
description: PHP 配置指令参考
---

:::caution
PHP 配置指令需要 [Patch 0001](/zh/development/patches/) 才能生效。如果没有该补丁，指令会被解析但不会传递给 lsphp。
:::

## 指令

| 指令 | 语法 |
|------|------|
| `php_value` | `php_value name value` |
| `php_flag` | `php_flag name On\|Off` |
| `php_admin_value` | `php_admin_value name value` |
| `php_admin_flag` | `php_admin_flag name On\|Off` |

`php_admin_*` 变体不能被用户脚本覆盖。

## 示例

### WordPress 上传限制

```apache
php_value upload_max_filesize 64M
php_value post_max_size 64M
php_value memory_limit 256M
php_value max_execution_time 300
```

### 安全设置

```apache
php_flag display_errors Off
php_flag log_errors On
php_value error_log /var/log/php_errors.log
php_flag expose_php Off
```

### 会话配置

```apache
php_value session.save_path /tmp/sessions
php_value session.gc_maxlifetime 3600
php_flag session.use_strict_mode On
```
