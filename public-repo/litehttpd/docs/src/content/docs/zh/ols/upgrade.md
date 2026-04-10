---
title: "升级与降级"
description: "OpenLiteSpeed 升级和降级流程，包括备份与回滚。"
---

## 升级前准备

升级前请务必备份配置文件。

### 备份配置

```bash
# 创建带时间戳的备份
tar czf /root/ols-backup-$(date +%Y%m%d).tar.gz /usr/local/lsws/conf/
```

这将保存 `httpd_config.conf`、所有虚拟主机配置、SSL 设置和模块配置。

### 检查当前版本

```bash
/usr/local/lsws/bin/lshttpd -v
```

## 通过仓库升级

### AlmaLinux / Rocky Linux 9

```bash
dnf update openlitespeed
systemctl restart lsws
```

### Ubuntu / Debian

```bash
apt update
apt upgrade openlitespeed
systemctl restart lsws
```

包管理器会保留您现有的配置文件。如果配置文件在本地已被修改，系统可能会提示您选择保留当前版本还是安装包维护者的版本。除非您确定需要新的默认配置，否则请保留当前版本。

## 通过源码升级

如果您是从源码编译安装的：

```bash
cd openlitespeed
git pull
bash build.sh
cd dist
bash install.sh
/usr/local/lsws/bin/lswsctrl restart
```

安装脚本会检测到现有安装并保留配置文件。

## 降级

### 仓库降级

在 RPM 系系统上，使用 `dnf downgrade`：

```bash
# 列出可用版本
dnf --showduplicates list openlitespeed

# 降级到指定版本
dnf downgrade openlitespeed-1.7.19
systemctl restart lsws
```

在 Debian 系系统上：

```bash
# 列出可用版本
apt-cache showpkg openlitespeed

# 安装指定版本
apt install openlitespeed=1.7.19-1
systemctl restart lsws
```

### 源码降级

切换到目标标签并重新编译：

```bash
cd openlitespeed
git checkout v1.7.19
bash build.sh
cd dist
bash install.sh
/usr/local/lsws/bin/lswsctrl restart
```

## 从备份恢复

如果升级导致问题，恢复配置备份：

```bash
# 停止服务器
systemctl stop lsws

# 恢复配置
tar xzf /root/ols-backup-YYYYMMDD.tar.gz -C /

# 重新启动
systemctl start lsws
```

## 升级后验证

```bash
# 检查版本
/usr/local/lsws/bin/lshttpd -v

# 检查服务器状态
/usr/local/lsws/bin/lswsctrl status

# 查看错误日志中的启动问题
tail -50 /usr/local/lsws/logs/error.log
```

## 下一步

- [基础配置](/zh/ols/basic-config/) -- 升级后检查配置。
- [日志](/zh/ols/logs/) -- 检查日志中的升级后问题。
