---
title: "Basic Configuration"
description: "Understanding the OpenLiteSpeed main configuration file and key server settings."
---

OpenLiteSpeed uses a plain-text configuration file at `/usr/local/lsws/conf/httpd_config.conf`. Most settings can also be managed through the WebAdmin GUI at `https://your-server-ip:7080`.

## Configuration File Structure

The main config file is organized into several blocks:

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

## Key Server Settings

### Server Identity

```apacheconf
serverName                your-hostname.example.com
user                      nobody
group                     nobody
```

The `user` and `group` determine the OS user under which worker processes run. Use a non-root, unprivileged user.

### Tuning

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

Important tuning parameters:

- **maxConnections** -- Maximum concurrent connections.
- **connTimeout** -- Idle connection timeout in seconds.
- **maxKeepAliveReq** -- Requests allowed per keep-alive connection.
- **keepAliveTimeout** -- How long to wait for the next request on a keep-alive connection.
- **maxReqBodySize** -- Maximum upload size. Increase for large file uploads.

### Access Control

```apacheconf
accessControl {
    allow                     ALL
}
```

This controls server-level IP access. Use `allow` and `deny` with IP addresses or CIDR ranges.

## Listeners

Listeners define which addresses and ports the server binds to. They are covered in detail in [Listeners](/ols/listeners/).

```apacheconf
listener Default {
    address                   *:8088
    secure                    0
}
```

## Virtual Host Mapping

Virtual hosts are mapped to listeners:

```apacheconf
vhostMap Default {
    vhost                     Example
    domain                    *
}
```

See [Virtual Hosts](/ols/virtual-hosts/) for full vhost configuration.

## Module Configuration

Modules are loaded in the `module` block:

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

To load the LiteHTTPD module for .htaccess support:

```apacheconf
module ols_htaccess {
    ls_enabled                1
}
```

## WebAdmin GUI

The WebAdmin interface at port 7080 provides a graphical way to manage all these settings. Changes made through WebAdmin are written back to the config files.

To restart the server after manual config file edits:

```bash
/usr/local/lsws/bin/lswsctrl restart
```

Or perform a graceful restart (zero-downtime):

```bash
kill -USR1 $(cat /tmp/lshttpd/lshttpd.pid)
```

## Configuration Validation

Before restarting, test your configuration:

```bash
/usr/local/lsws/bin/lshttpd -t
```

This checks for syntax errors without restarting the server.

## Next Steps

- [Virtual Hosts](/ols/virtual-hosts/) -- configure individual sites.
- [Listeners](/ols/listeners/) -- set up address and port bindings.
- [SSL / TLS](/ols/ssl/) -- enable HTTPS on listeners.
- [Logs](/ols/logs/) -- configure logging.
