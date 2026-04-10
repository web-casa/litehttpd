---
title: "Node.js"
description: "使用 OpenLiteSpeed 作为 Node.js 应用的反向代理"
---

## 概述

OpenLiteSpeed 可以作为 Node.js 应用的反向代理，处理 SSL 终止、静态文件服务和 HTTP/2，同时将动态请求转发给 Node.js 后端。

## 安装 Node.js

```bash
# 使用 NodeSource 仓库（LTS 版本）
curl -fsSL https://rpm.nodesource.com/setup_lts.x | bash -
dnf install nodejs

# 或在 Debian/Ubuntu 上
curl -fsSL https://deb.nodesource.com/setup_lts.x | bash -
apt install nodejs
```

## 创建示例应用

```bash
mkdir -p /var/www/nodeapp
cd /var/www/nodeapp
npm init -y
npm install express
```

创建 `app.js`：

```javascript
const express = require('express');
const app = express();
const PORT = 3000;

app.get('/', (req, res) => {
  res.send('Hello from Node.js behind OLS');
});

app.listen(PORT, '127.0.0.1', () => {
  console.log(`Server running on port ${PORT}`);
});
```

## OLS 代理配置

### 外部应用

在 `httpd_config.conf` 中将 Node.js 后端定义为 proxy 类型的外部处理器：

```apacheconf
extprocessor nodeapp {
  type                    proxy
  address                 127.0.0.1:3000
  maxConns                100
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
}
```

### 虚拟主机

```apacheconf
virtualhost nodeapp {
  vhRoot                  /var/www/nodeapp
  configFile              conf/vhosts/nodeapp/vhconf.conf
  allowSymbolLink         1
  enableScript            1
  restrained              1

  docRoot                 $VH_ROOT/public/

  index {
    indexFiles             index.html
  }
}
```

### 上下文（在 vhconf.conf 中）

将所有请求（或特定路径）路由到 Node.js 后端：

```apacheconf
# 代理所有请求
context / {
  type                    proxy
  handler                 nodeapp
  addDefaultCharset       off
}
```

仅代理 API 请求，同时直接提供静态文件：

```apacheconf
# 直接从 public/ 提供静态文件
context /static/ {
  location                $VH_ROOT/public/static/
  allowBrowse             1
  extraHeaders {
    set                   Cache-Control "public, max-age=31536000"
  }
}

# 将 API 请求代理到 Node.js
context /api/ {
  type                    proxy
  handler                 nodeapp
  addDefaultCharset       off
}

# 将其他所有请求代理到 Node.js
context / {
  type                    proxy
  handler                 nodeapp
  addDefaultCharset       off
}
```

## WebSocket 代理

OLS 支持 WebSocket 代理。使用 proxy 上下文时，WebSocket 升级请求会自动转发，无需特殊配置。

对于使用 Socket.IO 的应用：

```apacheconf
context /socket.io/ {
  type                    proxy
  handler                 nodeapp
  addDefaultCharset       off
}
```

确保 `initTimeout` 和连接超时设置足够高，以支持长连接的 WebSocket：

```apacheconf
extprocessor nodeapp {
  type                    proxy
  address                 127.0.0.1:3000
  maxConns                1000
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
}
```

## 使用 systemd 进行进程管理

创建 systemd 单元文件来管理 Node.js 进程：

```ini
# /etc/systemd/system/nodeapp.service
[Unit]
Description=Node.js Application
After=network.target

[Service]
Type=simple
User=nobody
Group=nobody
WorkingDirectory=/var/www/nodeapp
ExecStart=/usr/bin/node app.js
Restart=always
RestartSec=5
Environment=NODE_ENV=production
Environment=PORT=3000

[Install]
WantedBy=multi-user.target
```

```bash
systemctl enable --now nodeapp
```

### 使用 PM2

在生产部署中，PM2 提供集群模式、日志管理和零停机重载：

```bash
npm install -g pm2

# 以集群模式启动
pm2 start app.js -i max --name nodeapp

# 保存进程列表
pm2 save

# 生成启动脚本
pm2 startup systemd
```

## 转发请求头

要将真实客户端 IP 和协议传递给 Node.js，添加代理请求头：

```apacheconf
context / {
  type                    proxy
  handler                 nodeapp
  addDefaultCharset       off

  extraHeaders {
    set                   X-Forwarded-For %{REMOTE_ADDR}e
    set                   X-Forwarded-Proto %{HTTPS}e
    set                   X-Real-IP %{REMOTE_ADDR}e
  }
}
```

在 Node.js 应用中，信任代理：

```javascript
app.set('trust proxy', '127.0.0.1');
```

## 故障排除

**502 Bad Gateway：**
- 确认 Node.js 进程正在运行：`systemctl status nodeapp`
- 检查 Node.js 应用是否在配置的地址和端口上监听
- 查看 `/usr/local/lsws/logs/error.log`

**WebSocket 连接断开：**
- 增加外部处理器的 `maxConns`
- 确保没有中间代理或防火墙终止空闲连接

**响应缓慢：**
- 检查 `persistConn` 是否设置为 `1` 以复用连接
- 监控 Node.js 事件循环延迟
