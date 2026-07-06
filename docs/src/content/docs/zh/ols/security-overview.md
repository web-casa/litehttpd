---
title: "安全概述"
description: "OpenLiteSpeed 安全功能与加固选项概述"
---

## 安全层级

OpenLiteSpeed 在多个层面提供安全保护：

1. **内置保护** -- 连接限制、单客户端限流、抗 DDoS
2. **ModSecurity** -- Web 应用防火墙，支持 OWASP CRS
3. **访问控制** -- 服务器级、虚拟主机级和上下文级的 IP 限制
4. **reCAPTCHA** -- 服务器级机器人防护
5. **安全响应头** -- 用于浏览器端保护的 HTTP 响应头
6. **LiteHTTPD 模块** -- 基于 `.htaccess` 的安全指令（Require、Order/Allow/Deny、Header）

## 内置保护

OLS 在 `httpd_config.conf` 中配置了多种防滥用机制：

### 单客户端限流

```apacheconf
security {
  perClientConnLimit {
    staticReqsPerSec      40
    dynReqsPerSec         4
    outBandwidth          0
    inBandwidth           0
    softLimit             500
    hardLimit             1000
    blockBadReq           1
    gracePeriod           15
    banPeriod             60
  }
}
```

| 设置 | 说明 |
|------|------|
| `staticReqsPerSec` | 每个 IP 每秒最大静态文件请求数 |
| `dynReqsPerSec` | 每个 IP 每秒最大动态（PHP）请求数 |
| `softLimit` | 开始限流的连接数 |
| `hardLimit` | 拒绝新连接的连接数 |
| `blockBadReq` | 阻止格式错误的 HTTP 请求（设为 `1`） |
| `gracePeriod` | 封禁生效前的等待秒数 |
| `banPeriod` | 超出限制后 IP 被封禁的秒数 |

### 连接限制

```apacheconf
tuning {
  maxConnections          10000
  maxSSLConnections       10000
  connTimeout             300
  maxKeepAliveReq         10000
  keepAliveTimeout        5
}
```

### CGI 安全

OLS 默认不启用 CGI。如需使用，请谨慎限制：

```apacheconf
security {
  CGIRLimit {
    maxCGIInstances       20
    minUID                11
    minGID                10
    forceGID              0
  }
}
```

## 访问控制

OLS 在三个层级提供原生 IP 访问控制：

- **服务器级** -- 应用于所有请求
- **虚拟主机级** -- 应用于特定虚拟主机
- **上下文级** -- 应用于特定 URL 路径

详见 [访问控制](/zh/ols/security-access-control/)。

## ModSecurity（WAF）

OLS 内置 ModSecurity v3 支持，提供 Web 应用防火墙保护。结合 OWASP Core Rule Set，可防御 SQL 注入、XSS 等常见攻击。

详见 [ModSecurity](/zh/ols/security-modsecurity/)。

## reCAPTCHA 保护

OLS 可以在允许访问前向可疑客户端展示验证码挑战，提供服务器级 DDoS 防护，无需修改应用程序。

详见 [reCAPTCHA](/zh/ols/security-recaptcha/)。

## 安全响应头

HTTP 安全响应头指示浏览器启用 HSTS、CSP 和框架嵌入限制等保护功能。可通过 OLS 原生配置或使用 LiteHTTPD 模块通过 `.htaccess` 进行设置。

详见 [安全响应头](/zh/ols/security-headers/)。

## LiteHTTPD 模块安全功能

安装 LiteHTTPD 模块（`ols_htaccess.so`）后，可使用 Apache 管理员熟悉的基于 `.htaccess` 的安全控制：

### 认证与授权

```apacheconf
# .htaccess
<RequireAll>
  Require ip 192.168.1.0/24
  Require valid-user
</RequireAll>
```

### 旧版访问控制

```apacheconf
# .htaccess
Order Deny,Allow
Deny from all
Allow from 192.168.1.0/24
```

### 暴力破解防护

在上传目录中阻止 PHP 执行：

```apacheconf
# wp-content/uploads/.htaccess
<FilesMatch "\.php$">
  Require all denied
</FilesMatch>
```

### 通过 .htaccess 设置安全响应头

```apacheconf
Header set X-Frame-Options "SAMEORIGIN"
Header set X-Content-Type-Options "nosniff"
Header set Strict-Transport-Security "max-age=31536000; includeSubDomains"
```

## 推荐安全检查清单

- [ ] 更改 WebAdmin 默认密码（端口 7080）
- [ ] 将 WebAdmin 访问限制为受信任的 IP
- [ ] 启用单客户端限流
- [ ] 安装并配置 ModSecurity 及 OWASP CRS
- [ ] 设置安全响应头（HSTS、CSP、X-Frame-Options）
- [ ] 禁用目录列表（`autoIndex` 设为 `0`）
- [ ] 阻止访问敏感文件（`.env`、`.git`、`.htaccess`）
- [ ] 使用 TLS 1.2+ 并禁用弱加密套件（参见 [SSL/TLS](/zh/ols/ssl/)）
- [ ] 在单客户端限流配置中将 `blockBadReq` 设为 `1`
- [ ] 限制上传/静态目录中的 PHP 执行
