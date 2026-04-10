---
title: 从原生 OLS 迁移到 LiteHTTPD
description: 为现有 OpenLiteSpeed 安装添加 .htaccess 支持
---

## 概述

本页介绍如何将原生 OLS 升级到 LiteHTTPD（Full 版本）以获得最大的 .htaccess 兼容性。如果你想在不替换 OLS 二进制文件的情况下快速评估，可以先使用 LiteHTTPD-Thin：只需放入 `.so` 模块（下方步骤 1-3）并跳过补丁。之后可以随时升级到 Full 版本。

如果你已经在运行 OpenLiteSpeed（原生版本），添加 LiteHTTPD 即可获得完整的 `.htaccess` 支持，无需更改现有配置。原生 OLS 仅处理 44% 的 `.htaccess` 指令，LiteHTTPD 将其提升至 100%。

## 获得的功能

| 功能 | 原生 OLS | 加装 LiteHTTPD |
|------|---------|---------------|
| .htaccess 兼容性 | 44% (8/18) | **90%+ (18/18)** |
| `Require all denied` | 返回 200（失效） | 返回 403 |
| `Header set` | 被忽略 | 正常生效 |
| `RewriteRule [R=301]` | 404 | 301 重定向 |
| `Options -Indexes` | 404 | 403 |
| `FilesMatch` ACL | 被忽略 | 正常执行 |
| `AuthType Basic` | 不支持 | 完整支持 |
| PHP `php_value` | 不支持 | 支持（需要补丁） |
| 静态文件性能 | 基线 | -5%（可忽略） |
| 内存开销 | 基线 | +13 MB |

## 迁移步骤

1. **安装模块**

   ```bash
   cp litehttpd_htaccess.so /usr/local/lsws/modules/
   ```

2. **在 OLS 配置中启用**

   添加到 `/usr/local/lsws/conf/httpd_config.conf`：
   ```
   module litehttpd_htaccess {
       ls_enabled              1
   }
   ```

3. **为虚拟主机启用 .htaccess**

   添加到 `/usr/local/lsws/conf/vhosts/<name>/vhconf.conf`：
   ```
   allowOverride 255
   autoLoadHtaccess 1
   ```

4. **禁用 OLS 原生缓存（可选）**

   为了准确测试 .htaccess 头设置，可禁用内置缓存模块：
   ```
   # 在 httpd_config.conf 中设置 cache 模块为禁用：
   module cache {
       ls_enabled          0
   }
   ```

5. **重启 OLS**

   ```bash
   /usr/local/lsws/bin/lswsctrl restart
   ```

5. **验证**

   ```bash
   # 检查模块是否已加载
   grep "litehttpd" /usr/local/lsws/logs/error.log

   # 测试 .htaccess 处理
   echo 'Header set X-Test "OK"' > /var/www/html/.htaccess
   curl -I http://localhost:8088/
   # 应该显示：x-test: OK
   ```

## 可选：应用 OLS 补丁（从 Thin 升级到 Full）

如需完整功能支持（从 LiteHTTPD-Thin 升级到 Full），请应用 OLS 补丁并重新构建，或安装预构建的 RPM：

```bash
dnf install ./openlitespeed-litehttpd-*.rpm
```

或者手动应用 4 个 OLS 补丁并重新构建：

| 补丁 | 启用功能 | 是否必需 |
|------|---------|---------|
| 0001 PHPConfig | `php_value`, `php_flag` | 仅在 .htaccess 中使用 PHP INI 时需要 |
| 0002 Rewrite | `RewriteRule` 执行 | 仅在需要超出基本 WordPress 重写时需要 |
| 0003 readApacheConf | 启动时自动转换 Apache 配置 | 可选 |
| 0004 autoIndex | `Options -Indexes` 返回 403 | 推荐（安全加固） |

详见[补丁说明](/zh/development/patches/)。

## 不受影响的部分

- 现有 OLS 配置文件保持不变
- 虚拟主机设置、监听器和 SSL 配置不变
- PHP（lsphp）配置不变
- OLS 管理面板继续工作
- 无需替换 OLS 二进制文件（除非应用补丁）
