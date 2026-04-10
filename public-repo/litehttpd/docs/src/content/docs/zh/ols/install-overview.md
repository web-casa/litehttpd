---
title: "安装概述"
description: "OpenLiteSpeed 安装方式概述、支持的操作系统和系统要求。"
---

OpenLiteSpeed (OLS) 是一款轻量、高性能的开源 HTTP 服务器。本章节介绍可用的安装方式和系统要求。

## 支持的操作系统

| 操作系统 | 版本 | 包格式 |
|---|---|---|
| AlmaLinux / Rocky Linux | 9 | RPM |
| Ubuntu | 22.04 LTS, 24.04 LTS | DEB |
| Debian | 12 (Bookworm) | DEB |

其他兼容 RHEL 9 的发行版（Oracle Linux 9、CentOS Stream 9）也可通过 RPM 仓库安装。

## 最低系统要求

| 资源 | 最低要求 | 推荐配置 |
|---|---|---|
| 内存 | 1 GB | 2 GB+ |
| CPU | 1 核 | 2+ 核 |
| 磁盘 | 500 MB 可用空间 | 2 GB+ |
| 网络 | 公网 IP 或 localhost | 公网 IP 并开放 80、443、7080 端口 |

OLS 非常轻量，在同等负载下可以运行在比 Apache 或 Nginx 更小的实例上。

## 安装方式

### 仓库安装（推荐）

这是获得可用 OLS 服务器的最快途径。LiteSpeed 提供了包含预编译包的官方 RPM 和 APT 仓库。

请参阅[从仓库安装](/zh/ols/install-repository/)获取逐步指引。

### Docker

适用于容器化环境、CI/CD 流水线和快速评估。官方 Docker 镜像内置了 lsphp83。

请参阅 [Docker 部署](/zh/ols/install-docker/)了解详情。

### 从源码编译

适用于需要自定义编译选项、应用补丁或面向非标准平台的用户。

请参阅[从源码编译](/zh/ols/install-source/)获取编译说明。

## 默认安装目录结构

安装完成后，OLS 文件位于 `/usr/local/lsws/` 目录下：

```
/usr/local/lsws/
  bin/                  # 服务器二进制文件（lshttpd、lswsctrl）
  conf/                 # 配置文件
    httpd_config.conf   # 主服务器配置
    vhosts/             # 虚拟主机配置
  admin/                # WebAdmin 管理界面
  Example/              # 默认虚拟主机文档根目录
  logs/                 # 服务器日志
  modules/              # 可加载模块
  tmp/                  # 临时文件
```

## 安装后检查清单

无论使用哪种安装方式，安装完成后请完成以下步骤：

1. 设置 WebAdmin 密码（具体命令请参阅对应安装方式的页面）。
2. 通过 `https://your-server-ip:7080` 访问 WebAdmin 管理界面。
3. 配置您的第一个虚拟主机和监听器。
4. 设置 SSL/TLS 证书。请参阅 [SSL / TLS](/zh/ols/ssl/)。
5. 如有需要，安装 PHP。请参阅[安装 PHP](/zh/ols/php-install/)。

## 下一步

- [从仓库安装](/zh/ols/install-repository/) -- 大多数用户的推荐路径。
- [基础配置](/zh/ols/basic-config/) -- 了解主配置文件结构。
- [升级与降级](/zh/ols/upgrade/) -- 保持安装版本最新。
