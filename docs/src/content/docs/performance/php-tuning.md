---
title: PHP Performance Tuning
description: Optimize PHP performance with LiteHTTPD on OpenLiteSpeed
---

## Default vs Tuned PHP Performance

| Config | PHP RPS (wp-login.php) | Behavior |
|--------|----------------------|----------|
| Default (`CHILDREN=1`) | ~5 rps | Fork on demand, cold start ~1s |
| `CHILDREN=10` | ~15-20 rps | Pre-forked pool, no cold start |
| Apache PHP-FPM | ~16 rps | Pre-forked pool (reference) |

## Configuration

Add to your OLS `httpd_config.conf` extprocessor section:

```
extProcessor lsphp {
    env                     PHP_LSAPI_CHILDREN=10
    env                     PHP_LSAPI_MAX_IDLE=300
}
```

## Why is Default PHP Slow?

OLS uses lsphp via the LSAPI protocol. By default, a single lsphp process starts and forks new workers on demand. Each fork must load all PHP extensions and WordPress plugins (~1 second per cold start).

Setting `PHP_LSAPI_CHILDREN=10` pre-starts 10 workers, eliminating cold-start latency and matching Apache PHP-FPM throughput.
