---
title: "Listeners"
description: "Configure OpenLiteSpeed listeners for address binding, ports, SSL, and virtual host mapping."
---

Listeners define the network addresses and ports that OpenLiteSpeed binds to. Each listener can serve one or more virtual hosts and optionally terminate SSL/TLS.

## Listener Configuration

Listeners are defined in `/usr/local/lsws/conf/httpd_config.conf`:

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

## Listener Parameters

| Parameter | Description | Example |
|---|---|---|
| `address` | IP and port to bind. Use `*` for all interfaces. | `*:80`, `192.168.1.1:8080` |
| `secure` | Enable SSL/TLS. `0` = plain HTTP, `1` = HTTPS. | `1` |
| `keyFile` | Path to the private key file (when `secure` = 1). | `/path/to/privkey.pem` |
| `certFile` | Path to the certificate file (when `secure` = 1). | `/path/to/fullchain.pem` |
| `certChain` | Enable certificate chain. `1` = on. | `1` |

## Virtual Host Mapping

Map virtual hosts to a listener in `httpd_config.conf`:

```apacheconf
listener HTTP {
    address                 *:80
    secure                  0
    map                     example.com       example.com, www.example.com
    map                     app.example.com   app.example.com
}
```

The `map` directive syntax:

```
map  <virtual-host-name>  <domain1>, <domain2>, ...
```

The virtual host name must match the name defined in the `virtualhost` block. Domains can include wildcards:

```apacheconf
map                     example.com       *.example.com
```

## Multiple Listeners

You can define separate listeners for different purposes:

```apacheconf
# Public HTTP
listener HTTP {
    address                 *:80
    secure                  0
    map                     example.com example.com, www.example.com
}

# Public HTTPS
listener HTTPS {
    address                 *:443
    secure                  1
    keyFile                 /etc/letsencrypt/live/example.com/privkey.pem
    certFile                /etc/letsencrypt/live/example.com/fullchain.pem
    map                     example.com example.com, www.example.com
}

# Internal admin (bind to localhost only)
listener Admin {
    address                 127.0.0.1:8088
    secure                  0
    map                     internal internal.example.com
}
```

## IPv6 Support

OLS supports IPv6 addresses:

```apacheconf
listener HTTPv6 {
    address                 [::]:80
    secure                  0
    map                     example.com example.com
}
```

To listen on both IPv4 and IPv6, define separate listeners or use `[::]:80` which may accept both depending on your OS `net.ipv6.bindv6only` setting.

## SNI (Server Name Indication)

When multiple SSL virtual hosts share the same listener, OLS uses SNI to select the correct certificate. Each virtual host can specify its own certificate in the vhost config or via WebAdmin.

## Next Steps

- [SSL / TLS](/ols/ssl/) -- detailed SSL configuration for listeners.
- [Virtual Hosts](/ols/virtual-hosts/) -- configure the sites mapped to listeners.
- [Basic Configuration](/ols/basic-config/) -- other server-level settings.
