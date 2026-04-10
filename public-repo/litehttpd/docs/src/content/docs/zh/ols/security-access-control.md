---
title: "访问控制"
description: "OpenLiteSpeed 服务器级、虚拟主机级和目录级 IP 访问控制"
---

## 概述

OLS 在四个层级提供基于 IP 的访问控制：

1. **服务器级** -- 在 `httpd_config.conf` 中，应用于所有请求
2. **虚拟主机级** -- 在虚拟主机配置中，应用于单个站点
3. **上下文级** -- 按 URL 路径或目录
4. **.htaccess 级** -- 按目录，使用 LiteHTTPD 模块

## OLS 原生访问控制

### 语法

OLS 使用包含 `allow` 和 `deny` 指令的 `accessControl` 块：

```apacheconf
accessControl {
  allow                   192.168.1.0/24, 10.0.0.0/8
  deny                    ALL
}
```

格式说明：

- `allow` -- 逗号分隔的 IP 地址、CIDR 范围或 `ALL`
- `deny` -- 逗号分隔的 IP 地址、CIDR 范围或 `ALL`

OLS 先评估 `deny` 规则，再评估 `allow` 规则。如果请求匹配 `deny` 规则且不匹配任何 `allow` 规则，则被拒绝。

### 服务器级访问控制

在 `httpd_config.conf` 中：

```apacheconf
accessControl {
  allow                   ALL
  deny
}
```

### 虚拟主机级

在虚拟主机配置文件中：

```apacheconf
virtualhost example {
  ...
  accessControl {
    allow                 ALL
    deny                  192.168.1.100, 10.0.0.50
  }
}
```

### 上下文级

限制特定 URL 路径的访问：

```apacheconf
context /admin/ {
  location                $VH_ROOT/admin/
  allowBrowse             1

  accessControl {
    allow                 192.168.1.0/24
    deny                  ALL
  }
}
```

### WebAdmin 保护

将 WebAdmin 界面（端口 7080）限制为受信任的 IP：

```apacheconf
listener admin {
  address                 *:7080
  secure                  1

  accessControl {
    allow                 192.168.1.0/24, 127.0.0.1
    deny                  ALL
  }
}
```

## .htaccess 访问控制（LiteHTTPD）

安装 LiteHTTPD 模块后，可以在 `.htaccess` 文件中使用 Apache 兼容的访问控制指令。

### 现代语法（Require）

`Require` 指令是 Apache 2.4+ 的现代语法：

```apacheconf
# 允许特定 IP
Require ip 192.168.1.0/24

# 允许所有
Require all granted

# 拒绝所有
Require all denied

# 多个条件（全部必须匹配）
<RequireAll>
  Require ip 192.168.1.0/24
  Require valid-user
</RequireAll>

# 任一条件匹配即可
<RequireAny>
  Require ip 10.0.0.0/8
  Require ip 192.168.0.0/16
</RequireAny>

# 取反
<RequireAll>
  Require all granted
  <RequireNone>
    Require ip 192.168.1.100
  </RequireNone>
</RequireAll>
```

### IPv6 支持

LiteHTTPD 模块支持完整的 IPv6 CIDR 表示法：

```apacheconf
Require ip 2001:db8::/32
Require ip ::1
```

### 旧版语法（Order/Allow/Deny）

同时支持旧版 Apache 2.2 语法：

```apacheconf
# 默认拒绝，允许特定 IP
Order Deny,Allow
Deny from all
Allow from 192.168.1.0/24
Allow from 10.0.0.0/8

# 默认允许，拒绝特定 IP
Order Allow,Deny
Allow from all
Deny from 192.168.1.100
```

### 按文件限制

保护特定文件：

```apacheconf
# 阻止访问 .env 文件
<Files .env>
  Require all denied
</Files>

# 阻止所有点文件
<FilesMatch "^\.">
  Require all denied
</FilesMatch>

# 保护 wp-login.php
<Files wp-login.php>
  Require ip 192.168.1.0/24
</Files>
```

### 基于环境变量的访问

```apacheconf
# 根据环境变量允许访问
SetEnvIf User-Agent "MonitoringBot" allowed_bot
Require env allowed_bot
```

## 常见模式

### 阻止已知恶意 IP

在虚拟主机配置中创建拒绝列表：

```apacheconf
accessControl {
  allow                   ALL
  deny                    1.2.3.4, 5.6.7.0/24, 10.20.30.0/24
}
```

### 仅允许内网访问

```apacheconf
# OLS 原生
accessControl {
  allow                   10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 127.0.0.1
  deny                    ALL
}

# .htaccess 等效配置
Require ip 10.0.0.0/8 172.16.0.0/12 192.168.0.0/16 127.0.0.1
```

### 保护管理区域

```apacheconf
# 在 /wp-admin/ 的 .htaccess 中
<Files "*.php">
  <RequireAny>
    Require ip 192.168.1.0/24
    Require ip 10.0.0.0/8
  </RequireAny>
</Files>

# 允许所有用户访问 AJAX 请求
<Files admin-ajax.php>
  Require all granted
</Files>
```

### 国家级封锁

OLS 不包含内置的 GeoIP 功能。请使用外部防火墙（iptables、nftables 或 fail2ban）或 CDN 进行国家级封锁。

## 故障排除

**403 Forbidden 但 IP 应该被允许：**
- 检查不同层级（服务器 > 虚拟主机 > 上下文）是否有冲突的规则
- 在 `.htaccess` 中，确认 `Order` 指令符合预期
- 检查 IPv6 与 IPv4 不匹配的情况（客户端可能通过 IPv6 连接）

**来自 .htaccess 的访问控制不生效：**
- 确认 LiteHTTPD 模块已加载
- 检查虚拟主机中是否启用了 `autoLoadHtaccess`
- 确认 `.htaccess` 文件权限对 OLS 用户可读
