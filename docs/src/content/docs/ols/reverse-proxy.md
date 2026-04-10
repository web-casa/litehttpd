---
title: "Reverse Proxy"
description: "Configuring OpenLiteSpeed as a reverse proxy"
---

## Overview

OLS can act as a reverse proxy, forwarding client requests to one or more backend servers. This is useful for:

- Serving Node.js, Python, Go, or Java applications behind OLS
- SSL termination and HTTP/2 offloading
- Serving static files directly while proxying dynamic requests
- Load balancing across multiple backends

## External Application (Proxy Backend)

Define each backend as an extprocessor of type `proxy` in `httpd_config.conf`:

### HTTP Backend

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

### HTTPS Backend

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

### Unix Socket Backend

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

### Key Parameters

| Parameter | Description |
|-----------|-------------|
| `type` | Must be `proxy` |
| `address` | Backend address: `IP:port`, `https://IP:port`, or `uds://path` |
| `maxConns` | Max concurrent connections to backend |
| `initTimeout` | Seconds to wait for initial connection |
| `retryTimeout` | Seconds between retry attempts (0 = no retry) |
| `persistConn` | Reuse connections to backend (1 = enabled) |
| `respBuffer` | Buffer backend responses (0 = stream immediately) |

## Context Configuration

Route requests to the proxy backend using contexts in the virtual host configuration:

### Proxy All Requests

```apacheconf
context / {
  type                    proxy
  handler                 backend
  addDefaultCharset       off
}
```

### Proxy Specific Paths

```apacheconf
# API requests go to backend
context /api/ {
  type                    proxy
  handler                 backend
  addDefaultCharset       off
}

# Static files served directly by OLS
context /static/ {
  location                $VH_ROOT/static/
  allowBrowse             1

  extraHeaders {
    set                   Cache-Control "public, max-age=86400"
  }
}
```

### Proxy with URL Rewriting

Strip a prefix when forwarding to the backend:

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

## Forwarding Headers

Pass the real client IP and protocol to the backend:

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

## WebSocket Proxy

OLS automatically handles WebSocket upgrade requests through proxy contexts. No special configuration is needed -- the `Connection: Upgrade` and `Upgrade: websocket` headers are forwarded to the backend.

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

For long-lived WebSocket connections, ensure timeout values are high enough in the listener configuration.

## Backend Health Checking

OLS performs basic health checking on proxy backends:

- If a connection to the backend fails, OLS marks it as unavailable.
- After `retryTimeout` seconds, OLS attempts to reconnect.
- If `retryTimeout` is `0`, OLS retries on the next request.

For more sophisticated health checking, use an external tool like HAProxy or a load balancer in front of the backends.

## Multiple Backends

Define separate extprocessors for each backend and use different contexts:

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

## Timeouts

Configure timeouts to match your backend's response characteristics:

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

For long-running requests (file uploads, report generation), increase `initTimeout`.

## Troubleshooting

**502 Bad Gateway:**
- Verify the backend process is running and listening on the configured address
- Check `/usr/local/lsws/logs/error.log` for connection errors
- Ensure `maxConns` is not exhausted

**504 Gateway Timeout:**
- Increase `initTimeout` for slow backends
- Check backend application logs for long-running requests

**Incorrect client IP in backend logs:**
- Configure forwarding headers (`X-Forwarded-For`, `X-Real-IP`)
- Configure the backend to trust the proxy headers

**WebSocket disconnections:**
- Increase keep-alive timeouts
- Check for intermediate proxies or firewalls terminating idle connections
