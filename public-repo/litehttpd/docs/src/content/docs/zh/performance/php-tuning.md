---
title: PHP 性能调优
description: 在 OpenLiteSpeed 上使用 LiteHTTPD 优化 PHP 性能
---

## 默认与调优后的 PHP 性能

| 配置 | PHP RPS (wp-login.php) | 行为 |
|--------|----------------------|----------|
| 默认 (`CHILDREN=1`) | ~5 rps | 按需 fork，冷启动约 1 秒 |
| `CHILDREN=10` | ~15-20 rps | 预创建进程池，无冷启动 |
| Apache PHP-FPM | ~16 rps | 预创建进程池（参考） |

## 配置

添加到你的 OLS `httpd_config.conf` extprocessor 部分：

```
extProcessor lsphp {
    env                     PHP_LSAPI_CHILDREN=10
    env                     PHP_LSAPI_MAX_IDLE=300
}
```

## 为什么默认 PHP 很慢？

OLS 通过 LSAPI 协议使用 lsphp。默认情况下，单个 lsphp 进程启动并按需 fork 新的工作进程。每次 fork 都需要加载所有 PHP 扩展和 WordPress 插件（每次冷启动约 1 秒）。

设置 `PHP_LSAPI_CHILDREN=10` 可预启动 10 个工作进程，消除冷启动延迟，达到与 Apache PHP-FPM 相当的吞吐量。
