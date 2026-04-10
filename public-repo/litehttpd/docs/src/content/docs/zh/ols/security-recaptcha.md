---
title: "reCAPTCHA"
description: "OpenLiteSpeed 服务器级 reCAPTCHA 抗 DDoS 保护"
---

## 概述

OpenLiteSpeed 内置了 reCAPTCHA 功能，可在服务器级向可疑客户端展示验证码挑战。与应用级验证码不同，此功能在任何请求到达 PHP 或应用程序之前生效，能有效防御 DDoS 攻击和暴力破解机器人。

## 工作原理

1. OLS 通过单客户端限流系统监控每个客户端的请求速率。
2. 当客户端超过配置的阈值时，OLS 不会立即封锁，而是提供一个 reCAPTCHA 挑战页面。
3. 如果客户端通过了验证码，将设置一个验证 Cookie，后续请求正常处理。
4. 如果客户端未通过或忽略验证码，请求将继续被挑战。

这种方式比直接封禁更加用户友好，因为共享 IP（NAT、VPN、企业网络）后面的合法用户可以通过挑战。

## 通过 WebAdmin 配置

1. 导航至 **服务器配置 > 安全 > reCAPTCHA**
2. 配置以下设置：

| 设置 | 值 | 说明 |
|------|-----|------|
| **启用 reCAPTCHA** | Yes | 主开关 |
| **reCAPTCHA 类型** | Checkbox (v2) 或 Invisible (v2) | Checkbox 显示可见挑战；Invisible 仅对可疑行为发起挑战 |
| **Site Key** | （来自 Google） | 你的 reCAPTCHA 站点密钥 |
| **Secret Key** | （来自 Google） | 你的 reCAPTCHA 密钥 |
| **最大尝试次数** | 3 | 触发验证码前的失败尝试次数 |
| **允许的机器人请求数** | 5 | 每 10 秒触发前的请求数 |
| **机器人白名单** | （User-Agent 模式） | 白名单已知的合法机器人 |
| **连接限制** | 100 | 触发验证码的每 IP 连接数 |

3. 保存并执行优雅重启。

## 获取 reCAPTCHA 密钥

1. 访问 [Google reCAPTCHA 管理后台](https://www.google.com/recaptcha/admin)
2. 注册新站点
3. 选择 **reCAPTCHA v2**（Checkbox 或 Invisible）
4. 添加你的域名
5. 复制 **Site Key** 和 **Secret Key**

:::note
OLS 使用 reCAPTCHA v2。内置集成不支持 reCAPTCHA v3（基于评分）。
:::

## 通过配置文件配置

在 `httpd_config.conf` 中：

```apacheconf
security {
  reCAPTCHA {
    enabled               1
    type                  0
    siteKey               your_site_key_here
    secretKey             your_secret_key_here
    maxTries              3
    allowedRobotHits      5
    botWhiteList          Googlebot, Bingbot, baiduspider
    connLimit             100
    regConnLimit          15000
  }
}
```

### 参数说明

| 参数 | 值 | 说明 |
|------|-----|------|
| `enabled` | `0` / `1` | 启用或禁用 |
| `type` | `0`（checkbox）/ `1`（invisible） | reCAPTCHA 变体 |
| `siteKey` | 字符串 | Google reCAPTCHA 站点密钥 |
| `secretKey` | 字符串 | Google reCAPTCHA 密钥 |
| `maxTries` | 整数 | 触发验证码前的失败尝试次数 |
| `allowedRobotHits` | 整数 | 每 10 秒触发前的请求数 |
| `botWhiteList` | 逗号分隔 | 白名单 User-Agent 字符串 |
| `connLimit` | 整数 | 未知访客触发验证码的每 IP 连接数 |
| `regConnLimit` | 整数 | 回访/已验证访客触发验证码的每 IP 连接数 |

## 按虚拟主机覆盖

可以仅为特定虚拟主机启用 reCAPTCHA：

```apacheconf
virtualhost example {
  ...
  security {
    reCAPTCHA {
      enabled             1
      type                1
      siteKey             your_site_key_here
      secretKey           your_secret_key_here
    }
  }
}
```

## 机器人白名单

合法的搜索引擎机器人应绕过 reCAPTCHA。`botWhiteList` 设置接受 User-Agent 子字符串：

```apacheconf
botWhiteList          Googlebot, Bingbot, baiduspider, YandexBot, DuckDuckBot, Slurp
```

OLS 对 `User-Agent` 头进行不区分大小写的匹配。

## 测试

1. 临时将 `allowedRobotHits` 设为 `1` 以快速触发 reCAPTCHA。
2. 在浏览器中打开你的站点并快速刷新。
3. 你应该能看到 reCAPTCHA 挑战页面。
4. 完成验证码后确认后续请求正常处理。
5. 将 `allowedRobotHits` 恢复为适合生产环境的值。

## 故障排除

**reCAPTCHA 页面未出现：**
- 确认 `enabled` 设为 `1`
- 检查 `siteKey` 和 `secretKey` 是否正确
- 确保域名已在 Google reCAPTCHA 管理后台注册

**合法用户频繁收到验证码：**
- 增加 `allowedRobotHits` 和 `connLimit`
- 检查用户是否在共享 IP（NAT/VPN）后面，并增加 `regConnLimit`

**搜索引擎机器人被挑战：**
- 将机器人的 User-Agent 字符串添加到 `botWhiteList`
- 使用 `curl -A "Googlebot" https://example.com` 验证机器人是否绕过了挑战
