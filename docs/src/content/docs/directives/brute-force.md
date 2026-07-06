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
| `LSBruteForceTrustedProxy` | `IP/CIDR [IP/CIDR ...]` | (none) |
| `LSBruteForceWhitelist` | `IP/CIDR [IP/CIDR ...]` | (none) |
| `LSBruteForceProtectPath` | `/path` | (none) |

:::caution[X-Forwarded-For requires a trusted proxy]
`LSBruteForceXForwardedFor On` is only honoured when `LSBruteForceTrustedProxy`
is also set **and** the direct connection comes from one of those proxy CIDRs.
The client IP is then taken from the **rightmost** (proxy-added) entry of the
`X-Forwarded-For` header and validated as an IP address.

Without a trusted-proxy list, the `X-Forwarded-For` header is ignored and the
real peer IP is used. This prevents an attacker from spoofing `X-Forwarded-For`
to evade rate-limiting or impersonate a whitelisted address.
:::

## Example

### Protect WordPress Login

```apache
LSBruteForceProtection On
LSBruteForceAllowedAttempts 5
LSBruteForceWindow 600
LSBruteForceAction throttle
LSBruteForceThrottleDuration 10000
LSBruteForceXForwardedFor On
LSBruteForceTrustedProxy 10.0.0.0/8
LSBruteForceWhitelist 192.168.1.0/24 10.0.0.0/8
LSBruteForceProtectPath /wp-login.php
```

This throttles login attempts to 5 per 10 minutes, with a 10-second delay between throttled requests. Requests from the whitelisted subnets are exempt. Because `LSBruteForceTrustedProxy` is set, `X-Forwarded-For` is trusted only when the request arrives via `10.0.0.0/8`.
