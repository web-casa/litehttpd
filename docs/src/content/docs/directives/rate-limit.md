---
title: Rate Limiting
description: Per-IP request rate limiting
---

`RateLimit` throttles requests per client IP with a shared counter across
all OLS workers.

## Directives

| Directive | Syntax |
|-----------|--------|
| `RateLimit` | `N/M [burst=B] [action=deny\|throttle\|log]` |

- `N/M` — allow **N** requests per **M** seconds
- `burst=B` — tolerate B requests above the limit before acting
- `action=` — what to do when exceeded (default `deny`)
  - `deny` → 403 immediately
  - `throttle` → **429 + `Retry-After`** (never delays the worker)
  - `log` → count only, serve normally

## Example

```apache
RateLimit 100/60
```

100 requests per minute per IP; the 101st within the window is denied with
403.

```apache
RateLimit 10/10 burst=5 action=throttle
```

## Behaviour

| Action | Effect |
|--------|--------|
| `deny` | 403 immediately |
| `throttle` | progressive delay, then 429 |
| `log` | count and log, but serve |

Counters are namespaced separately from brute-force protection — heavy
browsing never feeds login-failure thresholds.
