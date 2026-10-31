---
title: 速率限制
description: 按客户端 IP 的请求限速
---

`RateLimit` 按 IP 限速，计数器在所有 OLS worker 间共享。

## 指令

| 指令 | 语法 |
|------|------|
| `RateLimit` | `N/M [burst=B] [action=deny\|throttle\|log]` |

- `N/M` —— 每 **M** 秒允许 **N** 个请求
- `burst=B` —— 超限后再容忍 B 个请求才动作
- `action=` —— 超限动作（默认 `deny`）

## 示例

```apache
RateLimit 100/60
```

每 IP 每分钟 100 个请求；窗口内第 101 个请求被 403 拒绝。

```apache
RateLimit 10/10 burst=5 action=throttle
```

## 行为

| 动作 | 效果 |
|------|------|
| `deny` | 立即 403 |
| `throttle` | 渐进延迟，随后 429 |
| `log` | 计数并记录，但正常服务 |

计数器与暴力破解保护的命名空间相互独立——重度浏览不会推高登录失败阈值。

> `action=throttle` 现在返回 **429 Too Many Requests** 并附带
> `Retry-After` 头（窗口重置前的秒数）。它绝不在 hook 里睡眠等待
> —— 睡眠会占住 worker，让单个客户端拖垮全部 worker（正是上游
> OLS issue #531 踩过的坑）。
