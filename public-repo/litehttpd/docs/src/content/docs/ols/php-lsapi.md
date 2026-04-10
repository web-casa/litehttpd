---
title: "PHP LSAPI (lsphp)"
description: "Understanding and configuring PHP LSAPI for OpenLiteSpeed"
---

## What is LSAPI?

LSAPI (LiteSpeed Server Application Programming Interface) is LiteSpeed's proprietary protocol for communicating with PHP. Unlike FastCGI, which serializes data into a binary stream over a socket, LSAPI uses shared memory for data transfer, reducing system call overhead and memory copies.

### LSAPI vs FastCGI

| Aspect | LSAPI (lsphp) | FastCGI (PHP-FPM) |
|--------|---------------|-------------------|
| Protocol | Shared memory + socket | Socket only |
| Process management | OLS manages workers | PHP-FPM manages workers |
| Performance | Faster (fewer copies) | Slightly slower |
| Memory | Lower per-request overhead | Higher per-request overhead |
| PHP opcode cache | Shared across children | Shared within FPM pool |
| Configuration | OLS extprocessor | php-fpm.conf + pool.d |
| suEXEC | Built-in support | Requires pool-per-user |

For most use cases, LSAPI is the recommended choice. Use PHP-FPM only when you need independent process management or compatibility with other web servers.

## How LSAPI Works

1. OLS starts the lsphp binary as a parent process.
2. The parent process forks `PHP_LSAPI_CHILDREN` worker processes.
3. Each worker handles one request at a time over a Unix domain socket.
4. After `LSAPI_MAX_REQS` requests, a worker exits and the parent forks a replacement.
5. Idle workers beyond `PHP_LSAPI_MAX_IDLE` seconds are terminated to reclaim memory.

## External Application Configuration

The lsphp external application is defined in `httpd_config.conf`:

```apacheconf
extprocessor lsphp {
  type                    lsapi
  address                 uds://tmp/lshttpd/lsphp.sock
  maxConns                10
  env                     PHP_LSAPI_CHILDREN=10
  env                     LSAPI_MAX_REQS=5000
  env                     PHP_LSAPI_MAX_IDLE=300
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
  autoStart               1
  path                    /usr/local/lsws/fcgi-bin/lsphp
  backlog                 100
  instances               1
  priority                0
  memSoftLimit            2047M
  memHardLimit            2047M
  procSoftLimit           1400
  procHardLimit           1500
}
```

### Key Parameters

| Parameter | Description |
|-----------|-------------|
| `type` | Must be `lsapi` for lsphp |
| `address` | Unix domain socket path. `uds://` prefix is required. |
| `maxConns` | Maximum concurrent connections. Should match `PHP_LSAPI_CHILDREN`. |
| `autoStart` | Set to `1` to let OLS start lsphp automatically. |
| `path` | Absolute path to the lsphp binary. |
| `instances` | Number of lsphp parent processes. Usually `1`. |
| `memSoftLimit` | Per-process memory soft limit. |
| `memHardLimit` | Per-process memory hard limit. |

## Script Handler

Map PHP file extensions to the lsphp external application:

```apacheconf
scripthandler {
  add                     lsapi:lsphp  php
}
```

This tells OLS to route all `.php` requests to the `lsphp` extprocessor.

## suEXEC Support

OLS supports running lsphp under different user accounts per virtual host. In the virtual host configuration:

```apacheconf
virtualhost example {
  ...
  extprocessor lsphp {
    type                  lsapi
    address               uds://tmp/lshttpd/example_lsphp.sock
    maxConns              5
    env                   PHP_LSAPI_CHILDREN=5
    autoStart             1
    path                  /usr/local/lsws/fcgi-bin/lsphp
    instances             1
  }

  scripthandler {
    add                   lsapi:lsphp  php
  }

  setUIDMode              2
}
```

Setting `setUIDMode` to `2` (suEXEC) causes OLS to start the lsphp process as the owner of the document root directory.

## Process Lifecycle Tuning

### Warm-up

When OLS starts, it creates the parent lsphp process, which then forks children on demand. The first request to each child incurs a cold-start penalty (loading PHP, extensions, opcode cache). To pre-warm:

```bash
# Set children to start immediately
env PHP_LSAPI_CHILDREN=10
```

### Graceful Restart

When you run `systemctl restart lsws`, OLS sends a graceful shutdown signal. Active PHP requests finish before workers exit. New workers are forked by the new parent process.

To restart only lsphp without restarting OLS:

```bash
killall -USR1 lsphp
```

This causes the parent lsphp to gracefully restart all children.

## Troubleshooting

**lsphp not starting:**
- Check that the binary exists at the configured `path`
- Verify the binary has execute permissions
- Check `/usr/local/lsws/logs/stderr.log` for PHP startup errors

**502 Bad Gateway:**
- The lsphp process crashed or is not running
- Check `maxConns` matches `PHP_LSAPI_CHILDREN`
- Review `/usr/local/lsws/logs/error.log`

**High memory usage:**
- Reduce `PHP_LSAPI_CHILDREN`
- Lower `LSAPI_MAX_REQS` to recycle workers more frequently
- Check for PHP memory leaks in application code

## Next Steps

- [PHP Environment Variables](/ols/php-env/) -- tune worker process behavior
- [Install PHP](/ols/php-install/) -- install additional PHP versions
