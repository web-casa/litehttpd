---
title: "安全响应头"
description: "在 OpenLiteSpeed 上配置安全响应头"
---

## 概述

安全响应头指示浏览器启用内置保护，防御点击劫持、XSS、MIME 嗅探和协议降级等常见攻击。OLS 支持原生设置响应头，也可通过 LiteHTTPD 模块使用 `.htaccess` 进行配置。

## 基本安全响应头

| 响应头 | 用途 |
|--------|------|
| `Strict-Transport-Security` | 强制 HTTPS 连接（HSTS） |
| `X-Frame-Options` | 防止点击劫持 |
| `X-Content-Type-Options` | 防止 MIME 类型嗅探 |
| `Content-Security-Policy` | 控制资源加载 |
| `Referrer-Policy` | 控制引用来源信息 |
| `Permissions-Policy` | 限制浏览器功能 |
| `X-XSS-Protection` | 旧版 XSS 过滤器（已弃用但仍有用） |

## OLS 原生配置

### 服务器级响应头

在 `httpd_config.conf` 中为所有虚拟主机应用响应头：

```apacheconf
extraHeaders {
  set Strict-Transport-Security "max-age=31536000; includeSubDomains; preload"
  set X-Frame-Options "SAMEORIGIN"
  set X-Content-Type-Options "nosniff"
  set Referrer-Policy "strict-origin-when-cross-origin"
  set Permissions-Policy "camera=(), microphone=(), geolocation=()"
  set X-XSS-Protection "1; mode=block"
}
```

### 按虚拟主机设置响应头

在虚拟主机配置中：

```apacheconf
virtualhost example {
  ...
  extraHeaders {
    set Strict-Transport-Security "max-age=31536000; includeSubDomains"
    set X-Frame-Options "DENY"
    set Content-Security-Policy "default-src 'self'; script-src 'self' 'unsafe-inline'"
  }
}
```

### 按上下文设置响应头

```apacheconf
context /api/ {
  ...
  extraHeaders {
    set Access-Control-Allow-Origin "https://example.com"
    set Access-Control-Allow-Methods "GET, POST, OPTIONS"
    set Access-Control-Allow-Headers "Content-Type, Authorization"
  }
}
```

## .htaccess 响应头（LiteHTTPD）

使用 LiteHTTPD 模块，可在 `.htaccess` 中使用标准 Apache `Header` 指令：

```apacheconf
# 设置响应头
Header set X-Frame-Options "SAMEORIGIN"
Header set X-Content-Type-Options "nosniff"
Header set Strict-Transport-Security "max-age=31536000; includeSubDomains"
Header set Referrer-Policy "strict-origin-when-cross-origin"
Header set Permissions-Policy "camera=(), microphone=(), geolocation=()"

# 追加到已有响应头
Header append X-Frame-Options "SAMEORIGIN"

# 移除响应头
Header unset X-Powered-By
Header unset Server

# 条件响应头
Header set X-Robots-Tag "noindex, nofollow" env=staging
```

## 响应头配置示例

### HSTS（HTTP 严格传输安全）

强制浏览器对你的域名的所有后续请求使用 HTTPS：

```apacheconf
Header set Strict-Transport-Security "max-age=31536000; includeSubDomains; preload"
```

- `max-age=31536000` -- 记住 1 年
- `includeSubDomains` -- 应用于所有子域名
- `preload` -- 选择加入浏览器预加载列表（在 hstspreload.org 提交）

:::caution
仅在确定所有子域名都支持 HTTPS 时才添加 `preload`。从预加载列表中移除可能需要数月时间。
:::

### 内容安全策略 (CSP)

控制浏览器允许加载的资源：

```apacheconf
# 基本限制策略
Header set Content-Security-Policy "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; font-src 'self'; connect-src 'self'; frame-ancestors 'none'"

# WordPress 友好策略
Header set Content-Security-Policy "default-src 'self'; script-src 'self' 'unsafe-inline' 'unsafe-eval'; style-src 'self' 'unsafe-inline'; img-src 'self' data: https:; font-src 'self' data:; connect-src 'self'"
```

使用 `Content-Security-Policy-Report-Only` 先测试而不阻止：

```apacheconf
Header set Content-Security-Policy-Report-Only "default-src 'self'; report-uri /csp-report"
```

### X-Frame-Options

防止你的站点被嵌入到 iframe 中（点击劫持保护）：

```apacheconf
# 阻止所有框架嵌入
Header set X-Frame-Options "DENY"

# 仅允许同源框架嵌入
Header set X-Frame-Options "SAMEORIGIN"
```

### X-Content-Type-Options

防止浏览器对响应的 MIME 类型进行嗅探：

```apacheconf
Header set X-Content-Type-Options "nosniff"
```

### Referrer-Policy

控制请求中发送的引用来源信息量：

```apacheconf
# 跨域请求仅发送来源
Header set Referrer-Policy "strict-origin-when-cross-origin"

# 从不发送引用来源
Header set Referrer-Policy "no-referrer"
```

### Permissions-Policy

限制摄像头、麦克风和地理位置等浏览器功能：

```apacheconf
Header set Permissions-Policy "camera=(), microphone=(), geolocation=(), payment=()"
```

### 移除信息泄露响应头

```apacheconf
Header unset X-Powered-By
Header unset Server
```

## 完整示例

### OLS 原生（httpd_config.conf）

```apacheconf
extraHeaders {
  set Strict-Transport-Security "max-age=31536000; includeSubDomains"
  set X-Frame-Options "SAMEORIGIN"
  set X-Content-Type-Options "nosniff"
  set Referrer-Policy "strict-origin-when-cross-origin"
  set Permissions-Policy "camera=(), microphone=(), geolocation=()"
  unset X-Powered-By
}
```

### .htaccess（LiteHTTPD）

```apacheconf
Header set Strict-Transport-Security "max-age=31536000; includeSubDomains"
Header set X-Frame-Options "SAMEORIGIN"
Header set X-Content-Type-Options "nosniff"
Header set Referrer-Policy "strict-origin-when-cross-origin"
Header set Permissions-Policy "camera=(), microphone=(), geolocation=()"
Header set Content-Security-Policy "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data: https:"
Header unset X-Powered-By
```

## 验证响应头

```bash
curl -I https://example.com
```

或使用在线工具如 [securityheaders.com](https://securityheaders.com) 扫描你的站点并获取评级。

## 故障排除

**响应中未出现响应头：**
- 对于 `.htaccess`：确认 LiteHTTPD 模块已加载且 `autoLoadHtaccess` 已启用
- 对于 OLS 原生：检查 `extraHeaders` 是否在正确的块中（服务器、虚拟主机或上下文）
- 配置更改后重启 OLS

**CSP 阻止了合法资源：**
- 先使用 `Content-Security-Policy-Report-Only`
- 在浏览器控制台中检查 CSP 违规消息
- 将被阻止的域名添加到相应的指令中
