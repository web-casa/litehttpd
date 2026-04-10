---
title: "基础配置"
description: "了解 OpenLiteSpeed 主配置文件及关键服务器设置。"
---

OpenLiteSpeed 使用位于 `/usr/local/lsws/conf/httpd_config.conf` 的纯文本配置文件。大多数设置也可以通过 `https://your-server-ip:7080` 的 WebAdmin 管理界面进行管理。

## 配置文件结构

主配置文件由以下几个块组成：

```
httpd_config.conf
  serverName
  user / group
  tuning { }
  accessControl { }
  listener(s) { }
  vhostMap { }
  module(s) { }
```

## 关键服务器设置

### 服务器标识

```apacheconf
serverName                your-hostname.example.com
user                      nobody
group                     nobody
```

`user` 和 `group` 决定工作进程以哪个操作系统用户身份运行。请使用非 root 的低权限用户。

### 性能调优

```apacheconf
tuning {
    maxConnections            10000
    maxSSLConnections         10000
    connTimeout               300
    maxKeepAliveReq           10000
    keepAliveTimeout          5
    sndBufSize                0
    rcvBufSize                0
    maxReqURLLen              32768
    maxReqHeaderSize          65536
    maxReqBodySize            2047M
    maxDynRespHeaderSize      32768
    maxDynRespSize            2047M
    maxCachedFileSize         4096
    totalInMemCacheSize       20M
    maxMMapFileSize           256K
    totalMMapCacheSize        40M
    useSendfile               1
    fileETag                  28
    SSLCryptoDevice           null
}
```

重要的调优参数：

- **maxConnections** -- 最大并发连接数。
- **connTimeout** -- 空闲连接超时时间（秒）。
- **maxKeepAliveReq** -- 每个 keep-alive 连接允许的请求数。
- **keepAliveTimeout** -- 在 keep-alive 连接上等待下一个请求的时间。
- **maxReqBodySize** -- 最大上传大小。对于大文件上传需要增大此值。

### 访问控制

```apacheconf
accessControl {
    allow                     ALL
}
```

此项控制服务器级别的 IP 访问。可使用 `allow` 和 `deny` 配合 IP 地址或 CIDR 范围。

## 监听器

监听器定义服务器绑定的地址和端口。详细内容请参阅[监听器](/zh/ols/listeners/)。

```apacheconf
listener Default {
    address                   *:8088
    secure                    0
}
```

## 虚拟主机映射

虚拟主机映射到监听器：

```apacheconf
vhostMap Default {
    vhost                     Example
    domain                    *
}
```

请参阅[虚拟主机](/zh/ols/virtual-hosts/)了解完整的虚拟主机配置。

## 模块配置

模块在 `module` 块中加载：

```apacheconf
module cache {
    ls_enabled                1
    checkPrivateCache         1
    checkPublicCache          1
    maxCacheObjSize           10000000
    maxStaleAge               200
    qsCache                   1
    reqCookieCache            1
    respCookieCache           1
}
```

如需加载 LiteHTTPD 模块以支持 .htaccess：

```apacheconf
module ols_htaccess {
    ls_enabled                1
}
```

## WebAdmin 管理界面

7080 端口的 WebAdmin 界面提供了图形化的方式来管理所有这些设置。通过 WebAdmin 所做的更改会写回配置文件。

手动编辑配置文件后重启服务器：

```bash
/usr/local/lsws/bin/lswsctrl restart
```

或执行平滑重启（零停机）：

```bash
kill -USR1 $(cat /tmp/lshttpd/lshttpd.pid)
```

## 配置验证

重启前，测试您的配置：

```bash
/usr/local/lsws/bin/lshttpd -t
```

这会检查语法错误但不会重启服务器。

## 下一步

- [虚拟主机](/zh/ols/virtual-hosts/) -- 配置各个站点。
- [监听器](/zh/ols/listeners/) -- 设置地址和端口绑定。
- [SSL / TLS](/zh/ols/ssl/) -- 在监听器上启用 HTTPS。
- [日志](/zh/ols/logs/) -- 配置日志记录。
