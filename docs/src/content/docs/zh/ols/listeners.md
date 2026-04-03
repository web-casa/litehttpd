---
title: "监听器"
description: "配置 OpenLiteSpeed 监听器的地址绑定、端口、SSL 和虚拟主机映射。"
---

监听器定义 OpenLiteSpeed 绑定的网络地址和端口。每个监听器可以服务一个或多个虚拟主机，并可选择终止 SSL/TLS。

## 监听器配置

监听器在 `/usr/local/lsws/conf/httpd_config.conf` 中定义：

```apacheconf
listener HTTP {
    address                 *:80
    secure                  0
}

listener HTTPS {
    address                 *:443
    secure                  1
    keyFile                 /etc/letsencrypt/live/example.com/privkey.pem
    certFile                /etc/letsencrypt/live/example.com/fullchain.pem
}
```

## 监听器参数

| 参数 | 说明 | 示例 |
|---|---|---|
| `address` | 绑定的 IP 和端口。使用 `*` 表示所有接口。 | `*:80`、`192.168.1.1:8080` |
| `secure` | 启用 SSL/TLS。`0` = 明文 HTTP，`1` = HTTPS。 | `1` |
| `keyFile` | 私钥文件路径（当 `secure` = 1 时使用）。 | `/path/to/privkey.pem` |
| `certFile` | 证书文件路径（当 `secure` = 1 时使用）。 | `/path/to/fullchain.pem` |
| `certChain` | 启用证书链。`1` = 开启。 | `1` |

## 虚拟主机映射

在 `httpd_config.conf` 中将虚拟主机映射到监听器：

```apacheconf
listener HTTP {
    address                 *:80
    secure                  0
    map                     example.com       example.com, www.example.com
    map                     app.example.com   app.example.com
}
```

`map` 指令语法：

```
map  <虚拟主机名>  <域名1>, <域名2>, ...
```

虚拟主机名必须与 `virtualhost` 块中定义的名称匹配。域名可以使用通配符：

```apacheconf
map                     example.com       *.example.com
```

## 多监听器

您可以为不同用途定义独立的监听器：

```apacheconf
# 公网 HTTP
listener HTTP {
    address                 *:80
    secure                  0
    map                     example.com example.com, www.example.com
}

# 公网 HTTPS
listener HTTPS {
    address                 *:443
    secure                  1
    keyFile                 /etc/letsencrypt/live/example.com/privkey.pem
    certFile                /etc/letsencrypt/live/example.com/fullchain.pem
    map                     example.com example.com, www.example.com
}

# 内部管理（仅绑定到 localhost）
listener Admin {
    address                 127.0.0.1:8088
    secure                  0
    map                     internal internal.example.com
}
```

## IPv6 支持

OLS 支持 IPv6 地址：

```apacheconf
listener HTTPv6 {
    address                 [::]:80
    secure                  0
    map                     example.com example.com
}
```

要同时监听 IPv4 和 IPv6，可定义独立的监听器，或使用 `[::]:80`（是否同时接受两种协议取决于操作系统的 `net.ipv6.bindv6only` 设置）。

## SNI（服务器名称指示）

当多个 SSL 虚拟主机共享同一个监听器时，OLS 使用 SNI 来选择正确的证书。每个虚拟主机可以在虚拟主机配置中或通过 WebAdmin 指定自己的证书。

## 下一步

- [SSL / TLS](/zh/ols/ssl/) -- 监听器的详细 SSL 配置。
- [虚拟主机](/zh/ols/virtual-hosts/) -- 配置映射到监听器的站点。
- [基础配置](/zh/ols/basic-config/) -- 其他服务器级别设置。
