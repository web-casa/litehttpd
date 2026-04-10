---
title: "Logs"
description: "Configure error logs, access logs, log levels, and log rotation in OpenLiteSpeed."
---

OpenLiteSpeed writes two types of logs: error logs and access logs. Both can be configured at the server level and overridden per virtual host.

## Log File Locations

| Log | Default Path |
|---|---|
| Server error log | `/usr/local/lsws/logs/error.log` |
| Server access log | `/usr/local/lsws/logs/access.log` |
| Stderr log | `/usr/local/lsws/logs/stderr.log` |
| Admin error log | `/usr/local/lsws/admin/logs/error.log` |

## Error Log Configuration

### Server-Level

In `/usr/local/lsws/conf/httpd_config.conf`:

```apacheconf
errorlog /usr/local/lsws/logs/error.log {
    logLevel                WARN
    debugLevel              0
    rollingSize             10M
    enableStderrLog         1
}
```

### Per-Virtual-Host

In the virtual host config (`vhconf.conf`):

```apacheconf
errorlog /usr/local/lsws/logs/example.com-error.log {
    useServer               0
    logLevel                WARN
    rollingSize             10M
}
```

Set `useServer` to `0` to use a vhost-specific log file, or `1` to write to the server-level error log.

## Log Levels

| Level | Description |
|---|---|
| `ERROR` | Errors only. Minimal output. |
| `WARN` | Warnings and errors. Recommended for production. |
| `NOTICE` | Normal but significant events. |
| `INFO` | Informational messages. |
| `DEBUG` | Debug output. Generates large volumes of log data. |

Set the level in the config:

```apacheconf
logLevel                WARN
```

For temporary debugging, set `debugLevel` to a non-zero value (0-10). Higher values produce more output:

```apacheconf
debugLevel              5
```

Reset `debugLevel` to `0` after troubleshooting.

## Access Log Configuration

### Server-Level

```apacheconf
accesslog /usr/local/lsws/logs/access.log {
    rollingSize             10M
    keepDays                30
    compressArchive         1
    logReferer              1
    logUserAgent            1
}
```

### Per-Virtual-Host

```apacheconf
accesslog /usr/local/lsws/logs/example.com-access.log {
    useServer               0
    logFormat               "%h %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-Agent}i\""
    rollingSize             10M
    keepDays                30
    compressArchive         1
}
```

### Log Format Variables

OLS uses Apache-compatible log format strings:

| Variable | Description |
|---|---|
| `%h` | Remote host (IP address) |
| `%l` | Remote logname (usually `-`) |
| `%u` | Authenticated user |
| `%t` | Timestamp |
| `%r` | Request line |
| `%>s` | Final HTTP status code |
| `%b` | Response size in bytes |
| `%{Referer}i` | Referer header |
| `%{User-Agent}i` | User-Agent header |
| `%D` | Request processing time in microseconds |
| `%T` | Request processing time in seconds |

## Log Rotation

OLS has built-in log rotation controlled by these parameters:

| Parameter | Description | Default |
|---|---|---|
| `rollingSize` | Rotate when the log file reaches this size | `10M` |
| `keepDays` | Delete rotated logs older than this many days | `30` |
| `compressArchive` | Compress rotated log files (`1` = on) | `0` |

Rotated files are named with a timestamp suffix, for example `error.log.2025_01_15`.

If you prefer external rotation with `logrotate`, disable built-in rotation by setting `rollingSize` to `0`:

```
/usr/local/lsws/logs/*.log {
    daily
    rotate 14
    compress
    missingok
    notifempty
    postrotate
        kill -USR1 $(cat /tmp/lshttpd/lshttpd.pid)
    endscript
}
```

## Viewing Logs

```bash
# Tail the error log
tail -f /usr/local/lsws/logs/error.log

# Search for errors
grep -i error /usr/local/lsws/logs/error.log | tail -20

# Check access patterns
awk '{print $1}' /usr/local/lsws/logs/access.log | sort | uniq -c | sort -rn | head -10
```

## Next Steps

- [Basic Configuration](/ols/basic-config/) -- other server settings.
- [Custom Error Pages](/ols/custom-errors/) -- configure error responses.
- [Virtual Hosts](/ols/virtual-hosts/) -- per-vhost log configuration.
