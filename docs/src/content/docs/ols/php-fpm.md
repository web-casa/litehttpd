---
title: "PHP-FPM Integration"
description: "Using PHP-FPM with OpenLiteSpeed as an alternative to lsphp"
---

## When to Use PHP-FPM

The native lsphp (LSAPI) is recommended for most deployments. Consider PHP-FPM when:

- You need to share the PHP process pool with another web server (e.g., during migration)
- Your hosting panel requires PHP-FPM (some control panels manage FPM pools exclusively)
- You need PHP-FPM-specific features like `access.log` per pool or `slowlog`
- You are running a multi-tenant setup where each user has an independent FPM pool

## Install PHP-FPM

**RHEL / AlmaLinux / Rocky:**

```bash
dnf install php-fpm
# or a specific version
dnf install php84-php-fpm
```

**Debian / Ubuntu:**

```bash
apt install php8.4-fpm
```

## Configure PHP-FPM

### TCP Socket (Port-based)

Edit `/etc/php-fpm.d/www.conf` (or `/etc/php/8.4/fpm/pool.d/www.conf` on Debian):

```ini
[www]
user = www-data
group = www-data
listen = 127.0.0.1:9000
pm = dynamic
pm.max_children = 20
pm.start_servers = 5
pm.min_spare_servers = 3
pm.max_spare_servers = 10
```

### Unix Socket (Recommended)

Unix sockets avoid TCP overhead and are preferred for same-host setups:

```ini
[www]
user = www-data
group = www-data
listen = /run/php-fpm/www.sock
listen.owner = nobody
listen.group = nobody
listen.mode = 0660
pm = dynamic
pm.max_children = 20
pm.start_servers = 5
pm.min_spare_servers = 3
pm.max_spare_servers = 10
```

Set `listen.owner` and `listen.group` to `nobody` (the user OLS runs as) so OLS can connect to the socket.

Start and enable PHP-FPM:

```bash
systemctl enable --now php-fpm
```

## Configure OLS to Use PHP-FPM

### External Application

Add a `fcgiapp` (FastCGI application) extprocessor in `httpd_config.conf`:

**TCP socket:**

```apacheconf
extprocessor phpfpm {
  type                    fcgiapp
  address                 127.0.0.1:9000
  maxConns                20
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
  autoStart               0
  path
  backlog                 100
  instances               1
}
```

**Unix socket:**

```apacheconf
extprocessor phpfpm {
  type                    fcgiapp
  address                 uds://run/php-fpm/www.sock
  maxConns                20
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
  autoStart               0
  path
  backlog                 100
  instances               1
}
```

Key differences from lsphp configuration:

- `type` is `fcgiapp` instead of `lsapi`
- `autoStart` is `0` because PHP-FPM manages its own processes
- `maxConns` should match `pm.max_children` in the FPM pool

### Script Handler

```apacheconf
scripthandler {
  add                     fcgi:phpfpm  php
}
```

Note the handler type is `fcgi` (not `lsapi`).

## Per-VHost PHP-FPM Pools

For multi-tenant hosting, create a separate FPM pool for each virtual host:

**PHP-FPM pool** (`/etc/php-fpm.d/example.conf`):

```ini
[example]
user = exampleuser
group = exampleuser
listen = /run/php-fpm/example.sock
listen.owner = nobody
listen.group = nobody
listen.mode = 0660
pm = dynamic
pm.max_children = 10
pm.start_servers = 2
pm.min_spare_servers = 1
pm.max_spare_servers = 5
```

**OLS virtual host configuration:**

```apacheconf
virtualhost example {
  ...
  extprocessor phpfpm_example {
    type                  fcgiapp
    address               uds://run/php-fpm/example.sock
    maxConns              10
    autoStart             0
    instances             1
  }

  scripthandler {
    add                   fcgi:phpfpm_example  php
  }
}
```

## PHP-FPM Status and Monitoring

Enable FPM status in the pool:

```ini
pm.status_path = /fpm-status
ping.path = /fpm-ping
```

Create an OLS context to allow access:

```apacheconf
context /fpm-status {
  type                    fcgi
  handler                 phpfpm
  accessControl {
    allow                 127.0.0.1
    deny                  ALL
  }
}
```

## Troubleshooting

**Connection refused:**
- Verify PHP-FPM is running: `systemctl status php-fpm`
- Check the socket file exists and has correct permissions
- Ensure `listen.owner`/`listen.group` matches the OLS user (`nobody`)

**502 errors under load:**
- Increase `pm.max_children` in the FPM pool
- Match OLS `maxConns` to `pm.max_children`
- Check FPM slow log for bottlenecks: `slowlog = /var/log/php-fpm/slow.log`

**File not found errors:**
- Verify `SCRIPT_FILENAME` is being passed correctly
- Ensure the document root is accessible to the FPM pool user

## Next Steps

- [PHP LSAPI](/ols/php-lsapi/) -- compare with native LSAPI performance
- [PHP Environment Variables](/ols/php-env/) -- tune PHP settings
