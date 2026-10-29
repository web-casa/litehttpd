---
title: 支持矩阵
description: LiteHTTPD 支持的平台、版本、安装包和功能边界
---

上线前先用本页确认 LiteHTTPD 版本和安装路径，避免在生产服务器上选错模式。

## 平台支持

| 环境 | CPU | LiteHTTPD-Full | LiteHTTPD-Thin | 说明 |
|------|-----|----------------|----------------|------|
| EL 8/9/10 兼容发行版 | x86_64 | 通过 `openlitespeed-litehttpd` RPM 支持 | 支持 | RPM 仓库配置脚本当前只接受 x86_64。 |
| Ubuntu 22.04+ / Debian 12+ | x86_64 | 从源码构建或使用匹配的发布资产 | 通过模块安装支持 | RPM 仓库面向 EL 系统。 |
| 官方 OpenLiteSpeed Docker 镜像 | x86_64 | 需要自定义镜像并替换为打补丁的 OLS 二进制 | 支持 | 对已有镜像来说 Thin 模式侵入最小。 |
| EL 系统上的 CyberPanel / aaPanel | x86_64 | 备份并验证后支持 | 支持 | 面板可能重新生成 vhost 文件；保持 `autoLoadHtaccess 0`。 |
| ARM64 / aarch64 | ARM64 | 仅源码构建 | 仅源码或模块构建 | 当前未发布 ARM64 RPM 仓库包。 |

## 版本功能支持

| 功能 | LiteHTTPD-Thin | LiteHTTPD-Full |
|------|----------------|----------------|
| 解析 83 种 Apache `.htaccess` 指令类型 | 支持 | 支持 |
| Header、ACL、认证、容器、条件块 | 支持 | 支持 |
| Redirect 和 RedirectMatch | 支持 | 支持 |
| BruteForceProtection 系列指令 | 支持 | 支持 |
| 执行 `.htaccess` 中的 RewriteRule | 仅解析 | 完整执行 |
| `php_value` / `php_flag` 透传给 lsphp | 仅解析 | 支持 |
| `Options -Indexes` 引擎层面 403 | 模块回退 | 原生 403 |
| `readApacheConf` 启动时转换 | 不可用 | 支持 |
| Apache 风格的逐目录处理器重映射 | 仅解析 | 仅解析 |

## 必需 OLS 设置

所有 LiteHTTPD 部署都需要启用模块：

```apacheconf
module litehttpd_htaccess {
    ls_enabled              1
}
```

需要读取 `.htaccess` 的每个虚拟主机都应设置：

```apacheconf
allowOverride 255
autoLoadHtaccess 0
```

LiteHTTPD 通过自己的模块钩子读取 `.htaccess`。保持 OLS 原生 `autoLoadHtaccess` 关闭，避免 `ErrorDocument`、`Options` 等指令被双重处理。

## 已知边界

- `AddHandler`、`SetHandler`、`RemoveHandler` 和 `Action` 会被解析以保持兼容，但实际请求处理仍由 OLS 的 `scriptHandler` 和 `extProcessor` 配置决定。
- Thin 模式适合安全头、ACL、认证、重定向和快速评估。站点依赖 `.htaccess` `RewriteRule` 执行或 `php_value` 时应使用 Full 模式。
- EL 8/9/10 x86_64 生产环境推荐使用 RPM 仓库安装。其他平台使用源码构建。
