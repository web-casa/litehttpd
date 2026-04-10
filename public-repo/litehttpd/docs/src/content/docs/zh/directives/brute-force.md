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
| `LSBruteForceWhitelist` | `IP/CIDR [IP/CIDR ...]` | （无） |
| `LSBruteForceProtectPath` | `/path` | （无） |

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
