---
title: "负载均衡"
description: "使用 OpenLiteSpeed 跨多个后端进行负载均衡"
---

## 概述

OLS 通过将请求分发到多个后端服务器来支持负载均衡。这通过包含多个代理后端的外部处理器组来配置。

## 定义后端服务器

首先，将每个后端定义为单独的外部处理器：

```apacheconf
extprocessor backend1 {
  type                    proxy
  address                 192.168.1.11:8080
  maxConns                100
  initTimeout             60
  retryTimeout            5
  persistConn             1
  respBuffer              0
}

extprocessor backend2 {
  type                    proxy
  address                 192.168.1.12:8080
  maxConns                100
  initTimeout             60
  retryTimeout            5
  persistConn             1
  respBuffer              0
}

extprocessor backend3 {
  type                    proxy
  address                 192.168.1.13:8080
  maxConns                100
  initTimeout             60
  retryTimeout            5
  persistConn             1
  respBuffer              0
}
```

## 负载均衡器配置

创建一个引用后端的负载均衡器外部处理器：

```apacheconf
extprocessor lb_pool {
  type                    loadbalancer
  workers                 backend1, backend2, backend3
  strategy                0
}
```

### 策略值

| 值 | 算法 | 说明 |
|----|------|------|
| `0` | 轮询 | 按顺序将请求均匀分发到所有后端 |
| `1` | 最少连接 | 将请求发送到活跃连接最少的后端 |
| `2` | 最快响应 | 将请求发送到响应时间最短的后端 |

## 将请求路由到负载均衡器

在上下文中使用负载均衡器，与使用单个代理后端相同：

```apacheconf
context / {
  type                    proxy
  handler                 lb_pool
  addDefaultCharset       off
}
```

## 加权分发

通过在 `workers` 指令中多次列出后端来分配不同的权重：

```apacheconf
extprocessor lb_weighted {
  type                    loadbalancer
  workers                 backend1, backend1, backend1, backend2, backend3
  strategy                0
}
```

在此示例中，`backend1` 接收 60% 的流量，`backend2` 和 `backend3` 各接收 20%。

## 会话保持（粘性会话）

对于在本地存储会话状态（而非共享存储）的应用，需要粘性会话确保客户端的请求始终发送到同一后端。

OLS 支持基于 Cookie 的会话保持。通过在后端设置会话 Cookie 或使用应用级会话 ID 来配置。

### IP 哈希方式

另一种方式是基于客户端 IP 使用一致性哈希。OLS 的负载均衡器不原生支持此配置，但可以通过以下方式实现：

1. 共享会话存储（Redis、Memcached 或数据库）
2. 应用级会话 Cookie 映射到特定后端

### 共享会话存储（推荐）

实现会话保持最可靠的方式是完全消除服务器端会话亲和性：

```ini
; PHP 会话存储到 Redis
session.save_handler = redis
session.save_path = "tcp://192.168.1.20:6379"
```

这允许任何后端处理任何请求，无论之前哪个后端处理了该会话。

## 健康检查

OLS 执行被动健康检查：

- 当连接后端失败时，OLS 将其标记为不可用。
- 经过 `retryTimeout` 秒后，OLS 再次尝试该后端。
- 如果重试成功，后端被标记为健康。

在每个后端上配置 `retryTimeout`：

```apacheconf
extprocessor backend1 {
  type                    proxy
  address                 192.168.1.11:8080
  maxConns                100
  retryTimeout            10
  ...
}
```

`retryTimeout` 为 `0` 意味着 OLS 在下一个请求时立即重试。

## 优雅移除后端

维护时移除后端：

1. 从负载均衡器配置的 `workers` 列表中移除。
2. 执行优雅重启：`systemctl restart lsws`
3. 到被移除后端的活跃连接将在旧进程退出前完成。

## 完整示例

```apacheconf
# 后端服务器
extprocessor app1 {
  type                    proxy
  address                 10.0.1.10:8080
  maxConns                200
  initTimeout             30
  retryTimeout            10
  persistConn             1
  respBuffer              0
}

extprocessor app2 {
  type                    proxy
  address                 10.0.1.11:8080
  maxConns                200
  initTimeout             30
  retryTimeout            10
  persistConn             1
  respBuffer              0
}

# 负载均衡器
extprocessor app_lb {
  type                    loadbalancer
  workers                 app1, app2
  strategy                1
}

# 虚拟主机
virtualhost example {
  ...
  context / {
    type                  proxy
    handler               app_lb
    addDefaultCharset     off
  }
}
```

## 故障排除

**负载分发不均匀：**
- 确认所有后端健康（检查 OLS 错误日志中的连接失败）
- 启用 `persistConn` 时，长连接可能导致分发偏差
- 考虑从轮询切换到最少连接策略

**单个后端接收所有流量：**
- 检查所有后端是否都列在 `workers` 中
- 确认其他后端可从 OLS 服务器访问
- 检查 `retryTimeout` -- 如果设置过高，失败的后端保持不可用时间过长

**切换后端时会话丢失：**
- 实施共享会话存储（Redis/Memcached）
- 或使用应用级粘性会话 Cookie
