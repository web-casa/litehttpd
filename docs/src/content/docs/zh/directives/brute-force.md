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

## 内核级升级（可选）

当[防火墙守护进程](/zh/ols/firewall/)在运行时，越限还会升级为
**nftables 内核丢包**——攻击者的数据包在到达 OpenLiteSpeed 之前即被
丢弃。通知对每个攻击者每个窗口恰好发送一次；封禁以窗口为 TTL 自动
过期。无守护进程时，上文的 403 就是完整行为。
