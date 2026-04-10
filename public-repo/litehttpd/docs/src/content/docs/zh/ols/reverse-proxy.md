---
title: "反向代理"
description: "将 OpenLiteSpeed 配置为反向代理"
---

## 概述

OLS 可以作为反向代理，将客户端请求转发到一个或多个后端服务器。适用场景：

- 在 OLS 后面运行 Node.js、Python、Go 或 Java 应用
- SSL 终止和 HTTP/2 卸载
- 直接提供静态文件，同时代理动态请求
- 跨多个后端进行负载均衡

## 外部应用（代理后端）

在 `httpd_config.conf` 中将每个后端定义为 `proxy` 类型的外部处理器：

### HTTP 后端

```apacheconf
extprocessor backend {
  type                    proxy
  address                 127.0.0.1:8080
  maxConns                100
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
}
```

### HTTPS 后端

```apacheconf
extprocessor secure_backend {
  type                    proxy
  address                 https://192.168.1.10:8443
  maxConns                100
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
}
```

### Unix 套接字后端

```apacheconf
extprocessor socket_backend {
  type                    proxy
  address                 uds://run/app/backend.sock
  maxConns                100
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
}
```

### 关键参数

| 参数 | 说明 |
|------|------|
| `type` | 必须为 `proxy` |
| `address` | 后端地址：`IP:端口`、`https://IP:端口` 或 `uds://路径` |
| `maxConns` | 到后端的最大并发连接数 |
| `initTimeout` | 等待初始连接的秒数 |
| `retryTimeout` | 重试间隔秒数（0 = 不重试） |
| `persistConn` | 复用到后端的连接（1 = 启用） |
| `respBuffer` | 缓冲后端响应（0 = 立即流式传输） |

## 上下文配置

在虚拟主机配置中使用上下文将请求路由到代理后端：

### 代理所有请求

```apacheconf
context / {
  type                    proxy
  handler                 backend
  addDefaultCharset       off
}
```

### 代理特定路径

```apacheconf
# API 请求发往后端
context /api/ {
  type                    proxy
  handler                 backend
  addDefaultCharset       off
}

# 静态文件由 OLS 直接提供
context /static/ {
  location                $VH_ROOT/static/
  allowBrowse             1

  extraHeaders {
    set                   Cache-Control "public, max-age=86400"
  }
}
```

### 带 URL 重写的代理

转发到后端时去除路径前缀：

```apacheconf
context /app/ {
  type                    proxy
  handler                 backend
  addDefaultCharset       off

  rewrite {
    enable                1
    rules                 <<<END_rules
      RewriteRule ^/app/(.*)$ /$1 [P]
    END_rules
  }
}
```

## 转发请求头

将真实客户端 IP 和协议传递给后端：

```apacheconf
context / {
  type                    proxy
  handler                 backend
  addDefaultCharset       off

  extraHeaders {
    set                   X-Forwarded-For %{REMOTE_ADDR}e
    set                   X-Forwarded-Proto %{HTTPS}e
    set                   X-Real-IP %{REMOTE_ADDR}e
    set                   Host %{HTTP_HOST}e
  }
}
```

## WebSocket 代理

OLS 通过 proxy 上下文自动处理 WebSocket 升级请求。无需特殊配置 -- `Connection: Upgrade` 和 `Upgrade: websocket` 头会自动转发到后端。

```apacheconf
extprocessor wsbackend {
  type                    proxy
  address                 127.0.0.1:3000
  maxConns                1000
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
}

context /ws/ {
  type                    proxy
  handler                 wsbackend
  addDefaultCharset       off
}
```

对于长连接的 WebSocket，确保监听器配置中的超时值足够高。

## 后端健康检查

OLS 对代理后端执行基本的健康检查：

- 如果连接后端失败，OLS 将其标记为不可用。
- 经过 `retryTimeout` 秒后，OLS 尝试重新连接。
- 如果 `retryTimeout` 为 `0`，OLS 在下一个请求时重试。

如需更精细的健康检查，可在后端前使用 HAProxy 等外部工具或负载均衡器。

## 多后端

为每个后端定义单独的外部处理器，使用不同的上下文：

```apacheconf
extprocessor api_backend {
  type                    proxy
  address                 127.0.0.1:8001
  maxConns                50
  persistConn             1
}

extprocessor web_backend {
  type                    proxy
  address                 127.0.0.1:8002
  maxConns                50
  persistConn             1
}
```

```apacheconf
context /api/ {
  type                    proxy
  handler                 api_backend
  addDefaultCharset       off
}

context / {
  type                    proxy
  handler                 web_backend
  addDefaultCharset       off
}
```

## 超时设置

根据后端的响应特征配置超时：

```apacheconf
extprocessor slow_backend {
  type                    proxy
  address                 127.0.0.1:8080
  maxConns                50
  initTimeout             120
  retryTimeout            5
  persistConn             1
  respBuffer              1
}
```

对于长时间运行的请求（文件上传、报告生成），增加 `initTimeout`。

## 故障排除

**502 Bad Gateway：**
- 确认后端进程正在运行且在配置的地址上监听
- 检查 `/usr/local/lsws/logs/error.log` 中的连接错误
- 确保 `maxConns` 未耗尽

**504 Gateway Timeout：**
- 为慢速后端增加 `initTimeout`
- 检查后端应用日志中的长时间运行请求

**后端日志中客户端 IP 不正确：**
- 配置转发请求头（`X-Forwarded-For`、`X-Real-IP`）
- 配置后端信任代理请求头

**WebSocket 断开连接：**
- 增加 keep-alive 超时
- 检查是否有中间代理或防火墙终止空闲连接
