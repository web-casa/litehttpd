---
title: 从 LSWS Enterprise 迁移到 LiteHTTPD
description: 使用 OpenLiteSpeed + LiteHTTPD 替代 LSWS Enterprise，获得同等 .htaccess 支持
---

## 为什么迁移？

LSWS Enterprise 提供完整的 `.htaccess` 支持，但需要付费许可证。LiteHTTPD（Full 版本）在免费开源的 OpenLiteSpeed 上实现了 90%+ 的兼容性——无需许可证费用。本页介绍如何使用 LiteHTTPD-Full 替代 LSWS，其中包括用于 RewriteRule 执行和 php_value 支持的打补丁 OLS 二进制文件。

## 功能对比

| 功能 | LSWS Enterprise | LiteHTTPD (OLS) |
|------|----------------|-----------------|
| .htaccess 兼容性 | 100% | **90%+** |
| 静态 RPS（无 .htaccess） | 24,786 | 22,104 |
| 静态 RPS（200 行 .htaccess） | 20,306 | 21,960 |
| .htaccess 开销 | -18.1% | **-0.7%** |
| 基线内存 | 819 MB | **676 MB** |
| 许可证 | 付费（从 $0/月免费层到 $65/月） | **免费 (GPLv3)** |
| 源代码 | 闭源 | **开源** |
| LSCache 内置 | 是 | 通过 OLS cache 模块 |
| 管理界面 | 是 | OLS WebAdmin（免费） |
| cPanel/Plesk 集成 | 是 | 不支持（手动配置） |

:::note
LiteHTTPD 的 .htaccess 解析开销实际上比 LSWS Enterprise **更低**（-0.7% vs -18.1%），得益于基于指纹的指令缓存机制。
:::

## 迁移步骤

1. **安装 OpenLiteSpeed**

   ```bash
   # AlmaLinux / Rocky
   rpm -Uvh https://rpms.litespeedtech.com/centos/litespeed-repo-1.3-1.el9.noarch.rpm
   dnf install openlitespeed

   # 安装打补丁的版本（LiteHTTPD-Full，推荐）
   dnf install ./openlitespeed-litehttpd-*.rpm
   ```

2. **安装 LiteHTTPD 模块**

   ```bash
   cp litehttpd_htaccess.so /usr/local/lsws/modules/
   ```

3. **迁移配置**

   LSWS 使用 XML 配置（`httpd_config.xml`），OLS 使用纯文本（`httpd_config.conf`）。关键映射：

   | LSWS (XML) | OLS (纯文本) |
   |-----------|-------------|
   | `<listener><address>*:80</address></listener>` | `listener Default { address *:80 }` |
   | `<virtualHost><vhRoot>/var/www</vhRoot></virtualHost>` | `virtualHost Example { vhRoot /var/www }` |
   | `<allowOverride>255</allowOverride>` | `allowOverride 255` |

4. **复制文档根目录**

   网站文件和 `.htaccess` 文件无需任何修改，只需将 OLS 虚拟主机指向相同目录：
   ```
   docRoot /var/www/html
   allowOverride 255
   autoLoadHtaccess 1
   ```

5. **配置 PHP**

   ```bash
   dnf install lsphp84 lsphp84-mysqlnd lsphp84-xml
   ln -sf /usr/local/lsws/lsphp84/bin/lsphp /usr/local/lsws/fcgi-bin/lsphp
   ```

   生产环境性能调优：
   ```
   extProcessor lsphp {
       env PHP_LSAPI_CHILDREN=10
       env PHP_LSAPI_MAX_IDLE=300
   }
   ```

6. **启用模块**

   ```
   module litehttpd_htaccess {
       ls_enabled 1
   }
   ```

7. **停止 LSWS，启动 OLS**

   ```bash
   /usr/local/lsws/bin/lswsctrl stop
   /usr/local/lsws/bin/lswsctrl start
   ```

## LiteHTTPD 中不可用的功能

| LSWS 功能 | LiteHTTPD 状态 | 替代方案 |
|-----------|---------------|---------|
| 内置页面缓存 (LSCache) | 模块不包含 | 启用 OLS `cache` 模块 + LiteSpeed Cache 插件 |
| cPanel/Plesk 集成 | 不支持 | 手动配置 OLS 或使用 CyberPanel |
| QUIC/HTTP3 | OLS 原生支持 | 无需更改 |
| ModSecurity | OLS 原生支持 | 无需更改 |
| ESI（边缘包含） | 不支持 | -- |
| GeoIP | 模块不包含 | 使用 OLS 原生 GeoIP |

## SSL 证书迁移

复制现有 SSL 证书并更新 OLS 监听器：

```
listener SSL {
    address *:443
    secure 1
    keyFile /etc/letsencrypt/live/example.com/privkey.pem
    certFile /etc/letsencrypt/live/example.com/fullchain.pem
}
```
