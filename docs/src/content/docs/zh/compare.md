---
title: 引擎对比
description: Apache、Stock OLS、LiteHTTPD、LSWS Enterprise 功能对比
---

## 总览

| | Apache httpd | Stock OLS | LiteHTTPD | LSWS Enterprise |
|-|-------------|-----------|-----------|-----------------|
| **价格** | 免费 | 免费 | **免费** | 起步价 $0（1 个域名） |
| **许可证** | Apache 2.0 | GPLv3 | **GPLv3** | 专有 |
| **源代码** | 开源 | 开源 | **开源** | 闭源 |
| **.htaccess 支持** | 完整 | 部分 | **90%+** | 完整 |
| **架构** | 每连接一进程 | 事件驱动 | **事件驱动** | 事件驱动 |

---

## .htaccess 指令支持

| 指令类别 | Apache | Stock OLS | LiteHTTPD | LSWS |
|---------|--------|-----------|-----------|------|
| Header set/unset/append | Yes | No | **Yes** | Yes |
| Require all denied | Yes | No（返回 200） | **Yes** | Yes |
| Options -Indexes | Yes | No（返回 404） | **Yes** | Yes |
| FilesMatch ACL | Yes | No | **Yes** | Yes |
| AuthType Basic | Yes | No | **Yes** | Yes |
| RewriteRule [R=301] | Yes | No（返回 404） | **Yes** | Yes |
| ExpiresByType | Yes | 部分 | **Yes** | Yes |
| If/ElseIf/Else | Yes | No | **Yes** | Yes |
| php_value/php_flag | Yes | No | **Yes**（需补丁） | Yes |
| ErrorDocument | Yes | No | **Yes** | Yes |
| SetEnv/SetEnvIf | Yes | No | **Yes** | Yes |
| AddType/ForceType | Yes | 部分 | **Yes** | Yes |

:::note
Stock OLS 仅通过 `RewriteFile` 处理 `.htaccess` 文件中的 RewriteRule。它不会执行 ACL、Header、Expires 或认证指令。这意味着像 `Require all denied` 这样的安全关键规则会静默失败（返回 200 而非 403）。
:::

---

## 性能 (4 vCPU, 8 GB RAM, AlmaLinux 9)

### 静态文件吞吐量 (Requests/sec)

| 场景 | Apache | LiteHTTPD | Stock OLS | LSWS |
|------|--------|-----------|-----------|------|
| 无 .htaccess | 11,082 | **22,104** | 23,242 | 24,786 |
| 简单 .htaccess（4 行） | 13,020 | **37,038** | 75,908 | 83,779 |
| 复杂 .htaccess（约 20 行） | 12,700 | **34,115** | 80,413 | 78,458 |
| WordPress .htaccess（约 200 行） | 10,618 | **21,960** | 18,883 | 20,306 |

:::caution
Stock OLS 和 LSWS 在简单/复杂 .htaccess 场景下 RPS 更高，是因为它们实际上并未执行 ACL/Header 指令。它们的安全规则会静默失败。
:::

### .htaccess 解析开销

| 引擎 | 无 .htaccess | 200 行 .htaccess | 开销 |
|------|-------------|-------------------|------|
| Apache | 11,082 | 10,618 | **-4.2%** |
| LiteHTTPD | 22,104 | 21,960 | **-0.7%** |
| Stock OLS | 23,242 | 18,883 | -18.8% |
| LSWS | 24,786 | 20,306 | -18.1% |

### 资源占用

| 指标 | Apache | LiteHTTPD | Stock OLS | LSWS |
|------|--------|-----------|-----------|------|
| 基础内存 | 969 MB | **676 MB** | 663 MB | 819 MB |
| 模块开销 | -- | **+13 MB** | -- | +156 MB |
| 峰值 CPU（静态） | 98.5% | **66.0%** | 48.1% | 38.8% |
| 每 CPU% 的 RPS | 525 | **1,324** | 1,890 | 1,265 |

---

## 安全功能对比

| 特性 | Apache | Stock OLS | LiteHTTPD | LSWS |
|------|--------|-----------|-----------|------|
| .htaccess ACL 执行 | Yes | **No** | Yes | Yes |
| .htpasswd 保护 | Yes | No | Yes | Yes |
| FilesMatch 拒绝 | Yes | **No（200）** | Yes（403） | Yes |
| 目录列表控制 | Yes | No | Yes | Yes |
| 路径穿越防御 | Yes | OLS 引擎 | **模块 + OLS** | Yes |
| ModSecurity | mod_security | OLS 模块 | OLS 模块 | 内置 |
| 抗 DDoS | 外部 | 有限 | 通过 OLS | 内置 |
| 暴力破解防护 | 外部 | No | **内置** | 内置 |

---

## 何时选择哪个引擎

| 使用场景 | 推荐 |
|----------|------|
| 最大程度的 Apache 兼容性，现有基础设施 | Apache httpd |
| 高性能且完整支持 .htaccess，免费许可证 | **LiteHTTPD** |
| 最小化 OLS，无需 .htaccess | Stock OLS |
| 企业支持，cPanel/Plesk 集成 | LSWS Enterprise |
| 预算有限的 WordPress 托管 | **LiteHTTPD** |
| 从 Apache 迁移到 OLS | **LiteHTTPD** |
| 替代付费 LSWS 许可证 | **LiteHTTPD** |
| Docker/容器环境中无法替换 OLS 二进制文件 | **LiteHTTPD-Thin** |

:::note
上表中的 LiteHTTPD 数据代表 Full 版本（通过 `openlitespeed-litehttpd` RPM 安装）。LiteHTTPD-Thin 提供相同的性能，但不支持 RewriteRule 执行和 php_value，因为它运行在未打补丁的原生 OLS 上。
:::
