---
title: 暴力破解防护
description: 暴力破解防护指令参考
---

## 指令

| 指令 | 语法 | 默认值 |
|------|------|--------|
| `LSBruteForceProtection` | `On\|Off` | Off |
| `LSBruteForceAllowedAttempts` | `N` | 10 |
| `LSBruteForceWindow` | `seconds` | 300 |
| `LSBruteForceAction` | `block\|throttle\|log` | block |
| `LSBruteForceThrottleDuration` | `milliseconds` | 5000 |
| `LSBruteForceXForwardedFor` | `On\|Off` | Off |
| `LSBruteForceTrustedProxy` | `IP/CIDR [IP/CIDR ...]` | （无） |
| `LSBruteForceWhitelist` | `IP/CIDR [IP/CIDR ...]` | （无） |
| `LSBruteForceProtectPath` | `/path` | （无） |

:::caution[X-Forwarded-For 需要可信代理]
仅当同时设置了 `LSBruteForceTrustedProxy` **且**直连来自这些代理 CIDR 时，`LSBruteForceXForwardedFor On` 才会生效。此时客户端 IP 取自 `X-Forwarded-For` 头的**最右侧**（由代理追加的）条目，并校验为合法 IP。

若未配置可信代理列表，则忽略 `X-Forwarded-For` 头，改用真实对端 IP。这可防止攻击者伪造 `X-Forwarded-For` 来规避限速或冒充白名单地址。
:::

## 示例

### 保护 WordPress 登录

```apache
LSBruteForceProtection On
LSBruteForceAllowedAttempts 5
LSBruteForceWindow 600
LSBruteForceAction throttle
LSBruteForceThrottleDuration 10000
LSBruteForceXForwardedFor On
LSBruteForceWhitelist 192.168.1.0/24 10.0.0.0/8
LSBruteForceProtectPath /wp-login.php
```

此配置将登录尝试限制为每 10 分钟 5 次，被限流的请求之间有 10 秒的延迟。来自白名单子网的请求不受限制。
