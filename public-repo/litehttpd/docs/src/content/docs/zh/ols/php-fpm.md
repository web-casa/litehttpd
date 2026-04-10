---
title: "PHP-FPM 集成"
description: "在 OpenLiteSpeed 中使用 PHP-FPM 作为 lsphp 的替代方案。"
---

## 何时使用 PHP-FPM

原生的 lsphp (LSAPI) 是大多数部署的推荐选择。在以下情况下可以考虑使用 PHP-FPM：

- 您需要与其他 Web 服务器共享 PHP 进程池（例如迁移期间）
- 您的主机面板要求使用 PHP-FPM（某些控制面板仅管理 FPM 池）
- 您需要 PHP-FPM 特有的功能，如每池 `access.log` 或 `slowlog`
- 您在多租户环境中运行，每个用户需要独立的 FPM 池

## 安装 PHP-FPM

**RHEL / AlmaLinux / Rocky：**

```bash
dnf install php-fpm
# 或指定版本
dnf install php84-php-fpm
```

**Debian / Ubuntu：**

```bash
apt install php8.4-fpm
```

## 配置 PHP-FPM

### TCP 套接字（基于端口）

编辑 `/etc/php-fpm.d/www.conf`（Debian 上为 `/etc/php/8.4/fpm/pool.d/www.conf`）：

```ini
[www]
user = www-data
group = www-data
listen = 127.0.0.1:9000
pm = dynamic
pm.max_children = 20
pm.start_servers = 5
pm.min_spare_servers = 3
pm.max_spare_servers = 10
```

### Unix 套接字（推荐）

Unix 套接字避免了 TCP 开销，在同一主机上部署时推荐使用：

```ini
[www]
user = www-data
group = www-data
listen = /run/php-fpm/www.sock
listen.owner = nobody
listen.group = nobody
listen.mode = 0660
pm = dynamic
pm.max_children = 20
pm.start_servers = 5
pm.min_spare_servers = 3
pm.max_spare_servers = 10
```

将 `listen.owner` 和 `listen.group` 设为 `nobody`（OLS 运行的用户），以便 OLS 能连接到套接字。

启动并启用 PHP-FPM：

```bash
systemctl enable --now php-fpm
```

## 配置 OLS 使用 PHP-FPM

### 外部应用程序

在 `httpd_config.conf` 中添加 `fcgiapp`（FastCGI 应用程序）外部处理器：

**TCP 套接字：**

```apacheconf
extprocessor phpfpm {
  type                    fcgiapp
  address                 127.0.0.1:9000
  maxConns                20
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
  autoStart               0
  path
  backlog                 100
  instances               1
}
```

**Unix 套接字：**

```apacheconf
extprocessor phpfpm {
  type                    fcgiapp
  address                 uds://run/php-fpm/www.sock
  maxConns                20
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
  autoStart               0
  path
  backlog                 100
  instances               1
}
```

与 lsphp 配置的主要区别：

- `type` 为 `fcgiapp` 而非 `lsapi`
- `autoStart` 为 `0`，因为 PHP-FPM 自行管理进程
- `maxConns` 应与 FPM 池中的 `pm.max_children` 匹配

### 脚本处理器

```apacheconf
scripthandler {
  add                     fcgi:phpfpm  php
}
```

注意处理器类型为 `fcgi`（不是 `lsapi`）。

## 按虚拟主机配置 PHP-FPM 池

对于多租户托管，为每个虚拟主机创建独立的 FPM 池：

**PHP-FPM 池**（`/etc/php-fpm.d/example.conf`）：

```ini
[example]
user = exampleuser
group = exampleuser
listen = /run/php-fpm/example.sock
listen.owner = nobody
listen.group = nobody
listen.mode = 0660
pm = dynamic
pm.max_children = 10
pm.start_servers = 2
pm.min_spare_servers = 1
pm.max_spare_servers = 5
```

**OLS 虚拟主机配置：**

```apacheconf
virtualhost example {
  ...
  extprocessor phpfpm_example {
    type                  fcgiapp
    address               uds://run/php-fpm/example.sock
    maxConns              10
    autoStart             0
    instances             1
  }

  scripthandler {
    add                   fcgi:phpfpm_example  php
  }
}
```

## PHP-FPM 状态和监控

在池配置中启用 FPM 状态：

```ini
pm.status_path = /fpm-status
ping.path = /fpm-ping
```

创建 OLS 上下文以允许访问：

```apacheconf
context /fpm-status {
  type                    fcgi
  handler                 phpfpm
  accessControl {
    allow                 127.0.0.1
    deny                  ALL
  }
}
```

## 故障排查

**连接被拒绝：**
- 验证 PHP-FPM 是否正在运行：`systemctl status php-fpm`
- 检查套接字文件是否存在且权限正确
- 确保 `listen.owner`/`listen.group` 与 OLS 用户（`nobody`）匹配

**高负载下出现 502 错误：**
- 增加 FPM 池中的 `pm.max_children`
- 使 OLS 的 `maxConns` 与 `pm.max_children` 匹配
- 检查 FPM 慢日志中的瓶颈：`slowlog = /var/log/php-fpm/slow.log`

**文件未找到错误：**
- 验证 `SCRIPT_FILENAME` 是否被正确传递
- 确保 FPM 池用户能访问文档根目录

## 下一步

- [PHP LSAPI](/zh/ols/php-lsapi/) -- 与原生 LSAPI 的性能对比
- [PHP 环境变量](/zh/ols/php-env/) -- 调优 PHP 设置
