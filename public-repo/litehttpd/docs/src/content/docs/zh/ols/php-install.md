---
title: "安装 PHP"
description: "使用 LiteSpeed 仓库包为 OpenLiteSpeed 安装 PHP。"
---

## 概述

OpenLiteSpeed 使用 **lsphp**（LiteSpeed PHP）作为默认的 PHP 处理器。lsphp 是针对 LiteSpeed 原生 LSAPI 协议优化的 PHP 构建版本，性能显著优于 PHP-FPM 或标准 FastCGI。

## 添加 LiteSpeed 仓库

### RHEL / AlmaLinux / Rocky Linux

```bash
rpm -Uvh https://rpms.litespeedtech.com/centos/litespeed-repo-1.3-1.el9.noarch.rpm
```

对于 EL8 系统，将 `el9` 替换为 `el8`。

### Debian / Ubuntu

```bash
wget -O - https://repo.litespeed.sh | sudo bash
```

## 安装 lsphp

### 可用版本

LiteSpeed 仓库提供以下 PHP 版本：

| 包名前缀 | PHP 版本 | 状态 |
|----------------|-------------|--------|
| `lsphp74` | 7.4.x | 仅安全修复 |
| `lsphp80` | 8.0.x | 已停止维护 |
| `lsphp81` | 8.1.x | 仅安全修复 |
| `lsphp82` | 8.2.x | 活跃支持 |
| `lsphp83` | 8.3.x | 活跃支持 |
| `lsphp84` | 8.4.x | 活跃支持（推荐） |

### 安装 PHP 8.4 及常用扩展

**RHEL / AlmaLinux / Rocky：**

```bash
dnf install lsphp84 lsphp84-mysqlnd lsphp84-xml lsphp84-gd \
  lsphp84-mbstring lsphp84-opcache lsphp84-curl lsphp84-json \
  lsphp84-zip lsphp84-bcmath lsphp84-intl
```

**Debian / Ubuntu：**

```bash
apt install lsphp84 lsphp84-mysql lsphp84-xml lsphp84-gd \
  lsphp84-mbstring lsphp84-opcache lsphp84-curl \
  lsphp84-zip lsphp84-bcmath lsphp84-intl
```

### 列出所有可用扩展

```bash
# RHEL 系
dnf list lsphp84-*

# Debian 系
apt-cache search lsphp84
```

## 将 lsphp 链接到 OLS

OLS 在 `/usr/local/lsws/fcgi-bin/lsphp` 路径查找 PHP 二进制文件。创建指向已安装版本的符号链接：

```bash
ln -sf /usr/local/lsws/lsphp84/bin/lsphp /usr/local/lsws/fcgi-bin/lsphp
```

以后如需切换 PHP 版本，更新此符号链接指向不同的 lsphp 二进制文件（例如 `lsphp83`）即可。

## 验证安装

```bash
/usr/local/lsws/lsphp84/bin/lsphp -v
```

预期输出：

```
PHP 8.4.x (litespeed) ...
```

注意 `(litespeed)` SAPI 标识 -- 这确认已编译了 LSAPI 支持。

## 多 PHP 版本

您可以同时安装多个 lsphp 版本，并将它们分配给不同的虚拟主机。安装每个版本：

```bash
dnf install lsphp83 lsphp84
```

然后在 `httpd_config.conf` 中为每个版本配置单独的外部应用程序，指向正确的二进制路径：

```apacheconf
extprocessor lsphp83 {
  type                    lsapi
  address                 uds://tmp/lshttpd/lsphp83.sock
  maxConns                10
  env                     PHP_LSAPI_CHILDREN=10
  initTimeout             60
  retryTimeout            0
  respBuffer              0
  autoStart               1
  path                    /usr/local/lsws/lsphp83/bin/lsphp
  backlog                 100
  instances               1
}
```

## PHP 配置

每个 lsphp 版本的 `php.ini` 位于：

```
/usr/local/lsws/lsphp84/etc/php/8.4/litespeed/php.ini
```

修改 `php.ini` 后，重启 OLS 以使更改生效：

```bash
systemctl restart lsws
```

## 下一步

- [PHP LSAPI](/zh/ols/php-lsapi/) -- 了解 LSAPI 的工作原理并进行调优
- [PHP 环境变量](/zh/ols/php-env/) -- 配置 PHP 工作进程
- [PHP-FPM 集成](/zh/ols/php-fpm/) -- 使用 PHP-FPM 作为替代方案
