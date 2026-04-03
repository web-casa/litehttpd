---
title: "控制面板"
description: "在主机控制面板中使用 OpenLiteSpeed"
---

## 概述

多个主机控制面板支持 OpenLiteSpeed，提供虚拟主机管理、SSL 证书、PHP 版本管理等图形界面。本页介绍最常见的集成方案。

## CyberPanel

CyberPanel 是专为 OpenLiteSpeed 构建的官方控制面板，提供最完整的 OLS 集成。

### 安装 CyberPanel + OLS

```bash
sh <(curl -s https://cyberpanel.net/install.sh)
```

安装过程中，在提示选择 Web 服务器时选择 **OpenLiteSpeed**。

### 功能

- 自动 OLS 虚拟主机管理
- 通过 Let's Encrypt 一键 SSL
- 按站点切换 PHP 版本
- LiteSpeed Cache 管理
- 邮件服务器（Postfix + Dovecot）
- DNS 服务器（PowerDNS）
- 文件管理器、数据库管理器、备份/恢复
- Docker 支持
- Git 部署

### OLS 配置

CyberPanel 自动管理 OLS 配置。配置文件位于：

- 服务器配置：`/usr/local/lsws/conf/httpd_config.conf`
- 虚拟主机配置：`/usr/local/lsws/conf/vhosts/{domain}/vhconf.conf`

:::caution
使用 CyberPanel 时避免直接编辑 OLS 配置文件。面板可能会覆盖你的更改。请使用 CyberPanel Web 界面或 CLI。
:::

### CyberPanel CLI

```bash
# 列出网站
cyberpanel listWebsites

# 创建网站
cyberpanel createWebsite --domainName example.com --email admin@example.com \
  --package Default --owner admin

# 签发 SSL
cyberpanel issueSSL --domainName example.com
```

### 在 CyberPanel 中使用 LiteHTTPD

LiteHTTPD 模块可与 CyberPanel 配合使用。将其安装到 OLS 模块目录后，应用程序（WordPress、Laravel 等）放置的 `.htaccess` 文件将自动被处理。

## aaPanel

aaPanel 是一个流行的控制面板，通过插件支持 OLS。

### 安装 aaPanel

```bash
# CentOS/AlmaLinux
URL=https://www.aapanel.com/script/install_7.0_en.sh && \
  if [ -f /usr/bin/curl ]; then curl -ksSO "$URL"; else wget --no-check-certificate -O install_7.0_en.sh "$URL"; fi && \
  bash install_7.0_en.sh aapanel
```

### 安装 OpenLiteSpeed 插件

1. 登录 aaPanel Web 界面。
2. 导航至 **应用商店**。
3. 搜索 "OpenLiteSpeed" 并安装。
4. 按照提示将默认 Web 服务器替换为 OLS。

### 配置

aaPanel 通过其 Web 界面管理 OLS 配置。关键路径：

- 服务器配置：`/usr/local/lsws/conf/httpd_config.conf`
- 虚拟主机配置：由 aaPanel 在 `/usr/local/lsws/conf/vhosts/` 下生成

## DirectAdmin

DirectAdmin 通过 CustomBuild 2.0 系统支持 OLS。

### 在 DirectAdmin 中切换到 OLS

```bash
cd /usr/local/directadmin/custombuild
./build set webserver openlitespeed
./build openlitespeed
./build rewrite_confs
```

### DirectAdmin 功能

- 自动虚拟主机配置
- 通过 CustomBuild 管理 PHP 版本
- SSL 证书管理
- 按用户资源隔离

### 切换回 Apache

```bash
cd /usr/local/directadmin/custombuild
./build set webserver apache
./build apache
./build rewrite_confs
```

## OLS WebAdmin 控制台

OLS 自带基于 Web 的管理控制台，独立于任何控制面板。

### 访问 WebAdmin

WebAdmin 界面默认运行在 7080 端口：

```
https://your-server-ip:7080
```

### 设置管理员密码

```bash
/usr/local/lsws/admin/misc/admpass.sh
```

### WebAdmin 功能

- 服务器配置（监听器、模块、调优）
- 虚拟主机管理
- 外部应用设置（PHP、代理后端）
- SSL 证书管理
- 实时服务器统计
- 日志查看器
- 优雅重启

### 安全

将 WebAdmin 访问限制为受信任的 IP：

```apacheconf
listener admin {
  address                 *:7080
  secure                  1

  accessControl {
    allow                 YOUR_IP_ADDRESS
    deny                  ALL
  }
}
```

## 面板对比

| 功能 | CyberPanel | aaPanel | DirectAdmin | OLS WebAdmin |
|------|-----------|---------|-------------|--------------|
| OLS 支持 | 原生 | 插件 | CustomBuild | 内置 |
| 多站点管理 | 是 | 是 | 是 | 是 |
| SSL 自动化 | 是 | 是 | 是 | 手动 |
| PHP 管理 | 是 | 是 | 是 | 手动 |
| 邮件服务器 | 是 | 是 | 是 | 否 |
| DNS 管理 | 是 | 否 | 是 | 否 |
| 文件管理器 | 是 | 是 | 是 | 否 |
| 数据库界面 | 是 | 是 | 是 | 否 |
| 免费版 | 是 | 是 | 否 | 是（始终免费） |
| 最适合 | OLS 优先的托管 | 通用托管 | ISP/经销商 | 单服务器 |

## 故障排除

**面板更改未生效：**
- 面板配置更改后重启 OLS：`systemctl restart lsws`
- 检查配置语法错误：`/usr/local/lsws/bin/lshttpd -t`

**面板无法连接到 OLS：**
- 确认 OLS 正在运行：`systemctl status lsws`
- 检查 WebAdmin 监听器是否在 7080 端口活跃

**面板与手动 OLS 编辑冲突：**
- 面板可能会覆盖手动配置更改
- 使用面板界面进行所有更改，或将特定虚拟主机排除在面板管理之外
