---
title: 处理器
description: LiteHTTPD 会解析的处理器映射指令
---

## 指令

| 指令 | 语法 | 行为 |
|------|------|------|
| `AddHandler` | `AddHandler handler-name extension [extension ...]` | 会被解析并记录；实际请求处理由 OLS 的 `scriptHandler` 配置控制 |
| `SetHandler` | `SetHandler handler-name` | 会被解析并记录；实际请求处理由 OLS 的 `scriptHandler` 配置控制 |
| `RemoveHandler` | `RemoveHandler extension [extension ...]` | 会被解析并记录；不会移除 OLS 的处理器映射 |
| `Action` | `Action media-type cgi-script` | 会被解析并记录；LiteHTTPD 不实现 CGI action 分发 |

## OLS 对应配置

OpenLiteSpeed 使用 vhost 或 server 配置中的 `scriptHandler` 和 `extProcessor` 将脚本映射到外部应用：

```apacheconf
scriptHandler {
  add                     lsapi:lsphp php
}

extProcessor lsphp {
  type                    lsapi
  address                 uds://tmp/lshttpd/lsphp.sock
  path                    /usr/local/lsws/lsphp84/bin/lsphp
}
```

:::note
这些指令会被接受，以便常见 Apache `.htaccess` 文件可以被解析而不是失败。它们在请求处理层面有意保持 no-op，因为 OLS 不通过 `.htaccess` 暴露 Apache 风格的逐目录处理器重映射。
:::
