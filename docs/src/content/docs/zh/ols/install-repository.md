---
title: "从仓库安装"
description: "从官方 RPM 和 APT 仓库安装 OpenLiteSpeed 的逐步指南。"
---

LiteSpeed 为 RPM 系和 Debian 系发行版维护了官方软件包仓库。这是推荐的安装方式。

## AlmaLinux / Rocky Linux 9 安装

### 添加仓库

```bash
rpm -Uvh https://rpms.litespeedtech.com/centos/litespeed-repo-1.3-1.el9.noarch.rpm
```

### 安装 OpenLiteSpeed

```bash
dnf install openlitespeed
```

### 安装 PHP（可选）

```bash
dnf install lsphp83 lsphp83-common lsphp83-mysqlnd lsphp83-opcache
```

## Ubuntu 22.04 / 24.04 安装

### 添加仓库并安装

```bash
wget -O - https://rpms.litespeedtech.com/debian/enable_lst_debian_repo.sh | bash
apt update
apt install openlitespeed
```

### 安装 PHP（可选）

```bash
apt install lsphp83 lsphp83-common lsphp83-mysql lsphp83-opcache
```

## Debian 12 安装

Debian 使用相同的仓库脚本：

```bash
wget -O - https://rpms.litespeedtech.com/debian/enable_lst_debian_repo.sh | bash
apt update
apt install openlitespeed
```

## 安装后配置

### 设置 WebAdmin 密码

安装完成后，为 WebAdmin 管理界面设置管理员密码：

```bash
/usr/local/lsws/admin/misc/admpass.sh
```

该脚本会提示输入用户名和密码。凭据存储在 `/usr/local/lsws/admin/conf/htpasswd` 中。

或者，可以在以下位置找到随机生成的初始密码：

```
/usr/local/lsws/adminpasswd
```

### 启动和停止服务器

```bash
# 启动
/usr/local/lsws/bin/lswsctrl start

# 停止
/usr/local/lsws/bin/lswsctrl stop

# 重启
/usr/local/lsws/bin/lswsctrl restart

# 查看状态
/usr/local/lsws/bin/lswsctrl status
```

在使用 systemd 的系统上：

```bash
systemctl start lsws
systemctl stop lsws
systemctl restart lsws
systemctl enable lsws   # 开机自启
```

### 访问 WebAdmin 管理界面

打开浏览器，访问：

```
https://your-server-ip:7080
```

接受自签名证书警告，然后使用上面设置的凭据登录。WebAdmin 界面允许您通过图形界面管理监听器、虚拟主机、SSL 和模块。

### 验证安装

默认站点在 8088 端口提供服务。打开：

```
http://your-server-ip:8088
```

您应该能看到 OpenLiteSpeed 的欢迎页面。

### 防火墙配置

如果使用 `firewalld`（AlmaLinux/Rocky）：

```bash
firewall-cmd --permanent --add-port=7080/tcp
firewall-cmd --permanent --add-port=80/tcp
firewall-cmd --permanent --add-port=443/tcp
firewall-cmd --reload
```

如果使用 `ufw`（Ubuntu/Debian）：

```bash
ufw allow 7080/tcp
ufw allow 80/tcp
ufw allow 443/tcp
```

## 下一步

- [基础配置](/zh/ols/basic-config/) -- 了解主配置文件。
- [虚拟主机](/zh/ols/virtual-hosts/) -- 设置您的第一个站点。
- [SSL / TLS](/zh/ols/ssl/) -- 启用 HTTPS。
