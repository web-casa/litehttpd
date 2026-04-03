---
title: "Load Balancing"
description: "Load balancing across multiple backends with OpenLiteSpeed"
---

## Overview

OLS supports load balancing by distributing requests across multiple backend servers. This is configured using extprocessor groups that contain multiple proxy backends.

## Define Backend Servers

First, define each backend as a separate extprocessor:

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

## Load Balancer Configuration

Create a load balancer extprocessor that references the backends:

```apacheconf
extprocessor lb_pool {
  type                    loadbalancer
  workers                 backend1, backend2, backend3
  strategy                0
}
```

### Strategy Values

| Value | Algorithm | Description |
|-------|-----------|-------------|
| `0` | Round Robin | Distributes requests evenly across all backends in order |
| `1` | Least Load | Sends requests to the backend with the fewest active connections |
| `2` | Fastest Response | Sends requests to the backend with the lowest response time |

## Route Requests to the Load Balancer

Use the load balancer in a context, just like a single proxy backend:

```apacheconf
context / {
  type                    proxy
  handler                 lb_pool
  addDefaultCharset       off
}
```

## Weighted Distribution

Assign different weights to backends by listing them multiple times in the `workers` directive:

```apacheconf
extprocessor lb_weighted {
  type                    loadbalancer
  workers                 backend1, backend1, backend1, backend2, backend3
  strategy                0
}
```

In this example, `backend1` receives 60% of traffic, while `backend2` and `backend3` each receive 20%.

## Session Persistence (Sticky Sessions)

For applications that store session state locally (not in a shared store), you need sticky sessions to ensure a client's requests always go to the same backend.

OLS supports cookie-based session persistence. Configure it by setting a session cookie in the backend or using an application-level session ID.

### IP Hash Approach

An alternative is to use consistent hashing based on client IP. This is not natively configurable in OLS's load balancer, but you can achieve it with:

1. A shared session store (Redis, Memcached, or database)
2. An application-level session cookie that maps to a specific backend

### Shared Session Store (Recommended)

The most reliable approach to session persistence is eliminating server-side session affinity entirely:

```ini
; PHP session in Redis
session.save_handler = redis
session.save_path = "tcp://192.168.1.20:6379"
```

This allows any backend to serve any request, regardless of which backend previously handled the session.

## Health Checking

OLS performs passive health checking:

- When a connection to a backend fails, OLS marks it as down.
- After `retryTimeout` seconds, OLS tries the backend again.
- If the retry succeeds, the backend is marked as healthy.

Configure `retryTimeout` on each backend:

```apacheconf
extprocessor backend1 {
  type                    proxy
  address                 192.168.1.11:8080
  maxConns                100
  retryTimeout            10
  ...
}
```

A `retryTimeout` of `0` means OLS retries immediately on the next request.

## Graceful Backend Removal

To remove a backend for maintenance:

1. Remove it from the `workers` list in the load balancer configuration.
2. Perform a graceful restart: `systemctl restart lsws`
3. Active connections to the removed backend will complete before the old process exits.

## Complete Example

```apacheconf
# Backend servers
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

# Load balancer
extprocessor app_lb {
  type                    loadbalancer
  workers                 app1, app2
  strategy                1
}

# Virtual host
virtualhost example {
  ...
  context / {
    type                  proxy
    handler               app_lb
    addDefaultCharset     off
  }
}
```

## Troubleshooting

**Uneven load distribution:**
- Verify all backends are healthy (check OLS error log for connection failures)
- With `persistConn` enabled, long-lived connections may skew distribution
- Consider switching from round-robin to least-load strategy

**Single backend receiving all traffic:**
- Check that all backends are listed in `workers`
- Verify other backends are reachable from the OLS server
- Review `retryTimeout` -- if set too high, failed backends stay down too long

**Session loss when switching backends:**
- Implement shared session storage (Redis/Memcached)
- Or use application-level sticky session cookies
