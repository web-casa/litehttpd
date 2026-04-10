---
title: 配置转换器 (confconv)
description: 将 Apache httpd.conf 转换为 OpenLiteSpeed 原生配置
---

`litehttpd-confconv` 将 Apache `httpd.conf` 和虚拟主机配置转换为 OpenLiteSpeed 格式。

## 用法

```bash
# 转换单个 Apache 配置
litehttpd-confconv --input /etc/httpd/conf/httpd.conf --output /usr/local/lsws/conf/apacheconf/

# 带端口映射（Apache 80 -> OLS 8088）
litehttpd-confconv --input /etc/httpd/conf/httpd.conf --output /usr/local/lsws/conf/apacheconf/ portmap=80:8088,443:8443

# 检查变更（用于脚本）
litehttpd-confconv --check /etc/httpd/conf/httpd.conf --state /tmp/confconv.state

# 监视模式（变更时自动重新编译）
litehttpd-confconv --watch /etc/httpd/conf/httpd.conf --interval 30 --output /usr/local/lsws/conf/apacheconf/
```

## 输出结构

```
/usr/local/lsws/conf/apacheconf/
  listeners.conf         # Listen 指令
  vhosts.conf            # VirtualHost 注册
  vhosts/
    example.com/
      vhconf.conf        # 每个虚拟主机的配置
```

## 支持的 Apache 指令

支持超过 60 个 Apache 配置指令，包括 ServerName、DocumentRoot、VirtualHost、Directory、Redirect、RewriteRule 等。
