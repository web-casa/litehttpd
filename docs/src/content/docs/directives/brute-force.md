---
title: Brute Force Protection
description: LiteSpeed brute force protection directives
---

## Directives

| Directive | Syntax | Default |
|-----------|--------|---------|
| `BruteForceProtection` | `On\|Off` | Off |
| `BruteForceAllowedAttempts` | `N` | 10 |
| `BruteForceWindow` | `seconds` | 300 |
| `BruteForceAction` | `block\|throttle\|log` | block |
| `BruteForceThrottleDuration` | `milliseconds` | 5000 |
| `BruteForceXForwardedFor` | `On\|Off` | Off |
| `BruteForceTrustedProxy` | `IP/CIDR [IP/CIDR ...]` | (none) |
| `BruteForceWhitelist` | `IP/CIDR [IP/CIDR ...]` | (none) |
| `BruteForceProtectPath` | `/path` | (none) |

:::caution[X-Forwarded-For requires a trusted proxy]
`BruteForceXForwardedFor On` is only honoured when `BruteForceTrustedProxy`
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

This throttles login attempts to 5 per 10 minutes, with a 10-second delay between throttled requests. Requests from the whitelisted subnets are exempt. Because `BruteForceTrustedProxy` is set, `X-Forwarded-For` is trusted only when the request arrives via `10.0.0.0/8`.

## Kernel-Level Escalation (optional)

When the [firewall daemon](/ols/firewall/) is running, a threshold crossing
also escalates to an **nftables kernel drop** — the attacker's packets are
discarded before reaching OpenLiteSpeed at all. The notification fires
exactly once per attacker per window; the block carries the window as its
TTL and expires automatically. Without the daemon, the 403 above is the
complete behaviour.
