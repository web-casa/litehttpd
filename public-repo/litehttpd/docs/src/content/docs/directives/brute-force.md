---
title: Brute Force Protection
description: LiteSpeed brute force protection directives
---

## Directives

| Directive | Syntax | Default |
|-----------|--------|---------|
| `LSBruteForceProtection` | `On\|Off` | Off |
| `LSBruteForceAllowedAttempts` | `N` | 10 |
| `LSBruteForceWindow` | `seconds` | 300 |
| `LSBruteForceAction` | `block\|throttle\|log` | block |
| `LSBruteForceThrottleDuration` | `milliseconds` | 5000 |
| `LSBruteForceXForwardedFor` | `On\|Off` | Off |
| `LSBruteForceWhitelist` | `IP/CIDR [IP/CIDR ...]` | (none) |
| `LSBruteForceProtectPath` | `/path` | (none) |

## Example

### Protect WordPress Login

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

This throttles login attempts to 5 per 10 minutes, with a 10-second delay between throttled requests. Requests from the whitelisted subnets are exempt.
