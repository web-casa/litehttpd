---
title: "日志"
description: "在 OpenLiteSpeed 中配置错误日志、访问日志、日志级别和日志轮转。"
---

OpenLiteSpeed 记录两种类型的日志：错误日志和访问日志。两者都可以在服务器级别配置，也可以按虚拟主机单独覆盖。

## 日志文件位置

| 日志 | 默认路径 |
|---|---|
| 服务器错误日志 | `/usr/local/lsws/logs/error.log` |
| 服务器访问日志 | `/usr/local/lsws/logs/access.log` |
| 标准错误日志 | `/usr/local/lsws/logs/stderr.log` |
| 管理端错误日志 | `/usr/local/lsws/admin/logs/error.log` |

## 错误日志配置

### 服务器级别

在 `/usr/local/lsws/conf/httpd_config.conf` 中：

```apacheconf
errorlog /usr/local/lsws/logs/error.log {
    logLevel                WARN
    debugLevel              0
    rollingSize             10M
    enableStderrLog         1
}
```

### 按虚拟主机

在虚拟主机配置（`vhconf.conf`）中：

```apacheconf
errorlog /usr/local/lsws/logs/example.com-error.log {
    useServer               0
    logLevel                WARN
    rollingSize             10M
}
```

将 `useServer` 设为 `0` 使用虚拟主机专属的日志文件，设为 `1` 则写入服务器级别的错误日志。

## 日志级别

| 级别 | 说明 |
|---|---|
| `ERROR` | 仅记录错误。输出最少。 |
| `WARN` | 记录警告和错误。生产环境推荐使用。 |
| `NOTICE` | 正常但重要的事件。 |
| `INFO` | 信息性消息。 |
| `DEBUG` | 调试输出。会产生大量日志数据。 |

在配置中设置级别：

```apacheconf
logLevel                WARN
```

临时调试时，将 `debugLevel` 设为非零值（0-10）。值越高，输出越详细：

```apacheconf
debugLevel              5
```

排查完成后，将 `debugLevel` 重置为 `0`。

## 访问日志配置

### 服务器级别

```apacheconf
accesslog /usr/local/lsws/logs/access.log {
    rollingSize             10M
    keepDays                30
    compressArchive         1
    logReferer              1
    logUserAgent            1
}
```

### 按虚拟主机

```apacheconf
accesslog /usr/local/lsws/logs/example.com-access.log {
    useServer               0
    logFormat               "%h %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-Agent}i\""
    rollingSize             10M
    keepDays                30
    compressArchive         1
}
```

### 日志格式变量

OLS 使用与 Apache 兼容的日志格式字符串：

| 变量 | 说明 |
|---|---|
| `%h` | 远程主机（IP 地址） |
| `%l` | 远程登录名（通常为 `-`） |
| `%u` | 已认证用户 |
| `%t` | 时间戳 |
| `%r` | 请求行 |
| `%>s` | 最终 HTTP 状态码 |
| `%b` | 响应大小（字节） |
| `%{Referer}i` | Referer 头 |
| `%{User-Agent}i` | User-Agent 头 |
| `%D` | 请求处理时间（微秒） |
| `%T` | 请求处理时间（秒） |

## 日志轮转

OLS 内置了日志轮转功能，由以下参数控制：

| 参数 | 说明 | 默认值 |
|---|---|---|
| `rollingSize` | 日志文件达到此大小时进行轮转 | `10M` |
| `keepDays` | 删除超过此天数的轮转日志 | `30` |
| `compressArchive` | 压缩轮转的日志文件（`1` = 开启） | `0` |

轮转后的文件以时间戳为后缀命名，例如 `error.log.2025_01_15`。

如果您更倾向使用外部的 `logrotate` 进行轮转，可将 `rollingSize` 设为 `0` 来禁用内置轮转：

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

## 查看日志

```bash
# 实时查看错误日志
tail -f /usr/local/lsws/logs/error.log

# 搜索错误
grep -i error /usr/local/lsws/logs/error.log | tail -20

# 查看访问模式
awk '{print $1}' /usr/local/lsws/logs/access.log | sort | uniq -c | sort -rn | head -10
```

## 下一步

- [基础配置](/zh/ols/basic-config/) -- 其他服务器设置。
- [自定义错误页](/zh/ols/custom-errors/) -- 配置错误响应。
- [虚拟主机](/zh/ols/virtual-hosts/) -- 按虚拟主机配置日志。
