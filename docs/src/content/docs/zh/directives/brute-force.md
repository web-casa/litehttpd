---
title: 暴力破解防护
description: 暴力破解防护指令参考
---

## 指令

| 指令 | 语法 | 默认值 |
|------|------|--------|
| `BruteForceProtection` | `On\|Off` | Off |
| `BruteForceAllowedAttempts` | `N` | 10 |
| `BruteForceWindow` | `seconds` | 300 |
| `BruteForceAction` | `block\|throttle\|log` | block |
| `BruteForceThrottleDuration` | `milliseconds` | 5000 |
| `BruteForceXForwardedFor` | `On\|Off` | Off |
| `BruteForceTrustedProxy` | `IP/CIDR [IP/CIDR ...]` | （无） |
| `BruteForceWhitelist` | `IP/CIDR [IP/CIDR ...]` | （无） |
| `BruteForceProtectPath` | `/path` | （无） |

:::caution[X-Forwarded-For 需要可信代理]
仅当同时设置了 `BruteForceTrustedProxy` **且**直连来自这些代理 CIDR 时，`BruteForceXForwardedFor On` 才会生效。此时客户端 IP 取自 `X-Forwarded-For` 头的**最右侧**（由代理追加的）条目，并校验为合法 IP。

若未配置可信代理列表，则忽略 `X-Forwarded-For` 头，改用真实对端 IP。这可防止攻击者伪造 `X-Forwarded-For` 来规避限速或冒充白名单地址。
:::

## 示例

### 保护 WordPress 登录

```apache
BruteForceProtection On
BruteForceAllowedAttempts 5
BruteForceWindow 600
BruteForceAction throttle
BruteForceThrottleDuration 10000
BruteForceXForwardedFor On
BruteForceTrustedProxy 10.0.0.0/8
BruteForceWhitelist 192.168.1.0/24 10.0.0.0/8
BruteForceProtectPath /wp-login.php
```

此配置将登录尝试限制为每 10 分钟 5 次，被限流的请求之间有 10 秒的延迟。来自白名单子网的请求不受限制。因为设置了 `BruteForceTrustedProxy`，只有请求来自 `10.0.0.0/8` 时才信任 `X-Forwarded-For`。
