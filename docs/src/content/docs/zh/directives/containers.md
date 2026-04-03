---
title: 容器指令
description: 容器指令指令参考
---

## 指令

| 指令 | 语法 |
|------|------|
| `<Files>` | `<Files "filename">` |
| `<FilesMatch>` | `<FilesMatch "regex">` |
| `<Limit>` | `<Limit METHOD [METHOD ...]>` |
| `<LimitExcept>` | `<LimitExcept METHOD [METHOD ...]>` |
| `<IfModule>` | `<IfModule [!]module_name>` |
| `<RequireAny>` | `<RequireAny>` |
| `<RequireAll>` | `<RequireAll>` |

## 示例

### 保护敏感文件

```apache
<Files "wp-config.php">
  Require all denied
</Files>

<FilesMatch "\.(bak|sql|log|env|git)$">
  Require all denied
</FilesMatch>
```

### HTTP 方法限制

```apache
<Limit POST PUT DELETE>
  Require all denied
</Limit>
```

### 条件模块加载

```apache
<IfModule mod_headers.c>
  Header set X-Frame-Options "SAMEORIGIN"
</IfModule>

<IfModule !mod_nonexistent.c>
  Header set X-Fallback "active"
</IfModule>
```

### 授权逻辑

```apache
<RequireAny>
  Require ip 192.168.1.0/24
  Require valid-user
</RequireAny>
```
