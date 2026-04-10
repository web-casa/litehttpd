---
title: "Node.js"
description: "Using OpenLiteSpeed as a reverse proxy for Node.js applications"
---

## Overview

OpenLiteSpeed can serve as a reverse proxy in front of Node.js applications, handling SSL termination, static file serving, and HTTP/2 while forwarding dynamic requests to the Node.js backend.

## Install Node.js

```bash
# Using NodeSource repository (LTS)
curl -fsSL https://rpm.nodesource.com/setup_lts.x | bash -
dnf install nodejs

# Or on Debian/Ubuntu
curl -fsSL https://deb.nodesource.com/setup_lts.x | bash -
apt install nodejs
```

## Create a Sample Application

```bash
mkdir -p /var/www/nodeapp
cd /var/www/nodeapp
npm init -y
npm install express
```

Create `app.js`:

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

## OLS Proxy Configuration

### External Application

Define the Node.js backend as a proxy extprocessor in `httpd_config.conf`:

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

### Virtual Host

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

### Context (in vhconf.conf)

Route all requests (or specific paths) to the Node.js backend:

```apacheconf
# Proxy all requests
context / {
  type                    proxy
  handler                 nodeapp
  addDefaultCharset       off
}
```

To proxy only API requests while serving static files directly:

```apacheconf
# Serve static files from public/
context /static/ {
  location                $VH_ROOT/public/static/
  allowBrowse             1
  extraHeaders {
    set                   Cache-Control "public, max-age=31536000"
  }
}

# Proxy API requests to Node.js
context /api/ {
  type                    proxy
  handler                 nodeapp
  addDefaultCharset       off
}

# Proxy everything else to Node.js
context / {
  type                    proxy
  handler                 nodeapp
  addDefaultCharset       off
}
```

## WebSocket Proxy

OLS supports WebSocket proxying. No special configuration is needed -- WebSocket upgrade requests are forwarded automatically when using a proxy context.

For applications using Socket.IO:

```apacheconf
context /socket.io/ {
  type                    proxy
  handler                 nodeapp
  addDefaultCharset       off
}
```

Ensure `initTimeout` and connection timeouts are set high enough for long-lived WebSocket connections:

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

## Process Management with systemd

Create a systemd unit to manage the Node.js process:

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

### Using PM2

For production deployments, PM2 provides clustering, log management, and zero-downtime reloads:

```bash
npm install -g pm2

# Start with cluster mode
pm2 start app.js -i max --name nodeapp

# Save process list
pm2 save

# Generate startup script
pm2 startup systemd
```

## Forwarding Headers

To pass the real client IP and protocol to Node.js, add proxy headers:

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

In your Node.js application, trust the proxy:

```javascript
app.set('trust proxy', '127.0.0.1');
```

## Troubleshooting

**502 Bad Gateway:**
- Verify the Node.js process is running: `systemctl status nodeapp`
- Check the Node.js app is listening on the configured address and port
- Review `/usr/local/lsws/logs/error.log`

**WebSocket connection drops:**
- Increase `maxConns` on the extprocessor
- Ensure no intermediate proxy or firewall is terminating idle connections

**Slow responses:**
- Check `persistConn` is set to `1` to reuse connections
- Monitor Node.js event loop lag
