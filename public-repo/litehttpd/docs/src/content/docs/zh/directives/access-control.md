---
title: 访问控制
description: 访问控制指令参考
---

访问控制指令用于限制谁可以访问你的站点或特定目录。LiteHTTPD 同时支持旧式的 `Order`/`Allow`/`Deny` 语法和现代的 `Require` 语法。

## 指令参考

| 指令 | 语法 | 说明 |
|------|------|------|
| `Order` | `Order Allow,Deny` 或 `Order Deny,Allow` | 设置 Allow/Deny 规则的评估顺序 |
| `Allow from` | `Allow from ip\|cidr\|all` | 允许来自指定地址的访问 |
| `Deny from` | `Deny from ip\|cidr\|all` | 拒绝来自指定地址的访问 |
| `Require all granted` | `Require all granted` | 允许所有人访问（现代语法） |
| `Require all denied` | `Require all denied` | 拒绝所有人访问（现代语法） |
| `Require ip` | `Require ip ip\|cidr [...]` | 允许来自指定 IP 或 CIDR 范围的访问 |
| `Require not ip` | `Require not ip ip\|cidr [...]` | 拒绝来自指定 IP 或 CIDR 范围的访问 |
| `Require env` | `Require env VAR` | 当指定的环境变量被设置时允许访问 |

## 示例

### 阻止目录访问（旧式语法）

```apache
Order Deny,Allow
Deny from all
Allow from 192.168.1.0/24
Allow from 10.0.0.0/8
```

### 阻止访问（现代语法）

```apache
Require all denied
```

### 仅允许特定 IP

```apache
Require ip 203.0.113.50
Require ip 2001:db8::/32
```

### 保护 WordPress 管理页面

```apache
<Files "wp-login.php">
  Order Deny,Allow
  Deny from all
  Allow from 203.0.113.50
</Files>
```

### 禁止上传目录执行 PHP

```apache
# 放置在 wp-content/uploads/.htaccess 中
<FilesMatch "\.php$">
  Require all denied
</FilesMatch>
```

### 基于环境变量的条件访问

```apache
SetEnvIf X-Forwarded-For "^10\." is_internal
Require env is_internal
```

:::note
所有基于 IP 的指令都支持 IPv4 和 IPv6 地址，包括 CIDR 表示法（例如 `192.168.0.0/16`、`2001:db8::/32`）。
:::

:::caution
使用 `Order Allow,Deny` 时，默认策略为拒绝。使用 `Order Deny,Allow` 时，默认策略为允许。请确保为你的使用场景选择正确的顺序。
:::
