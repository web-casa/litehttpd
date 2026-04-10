---
title: "SSL / TLS"
description: "为 OpenLiteSpeed 配置 SSL/TLS 证书、Let's Encrypt 集成和安全设置。"
---

## 监听器 SSL 配置

在 `/usr/local/lsws/conf/httpd_config.conf` 中为监听器启用 SSL：

```apacheconf
listener HTTPS {
    address                 *:443
    secure                  1
    keyFile                 /etc/letsencrypt/live/example.com/privkey.pem
    certFile                /etc/letsencrypt/live/example.com/fullchain.pem
    certChain               1
    sslProtocol             24
    ciphers                 ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384
    enableECDHE             1
    enableDHE               0
    map                     example.com example.com, www.example.com
}
```

### SSL 参数

| 参数 | 说明 |
|---|---|
| `keyFile` | PEM 编码私钥的路径 |
| `certFile` | PEM 编码证书的路径（使用 fullchain 以获得最佳兼容性） |
| `certChain` | 设为 `1` 启用证书链验证 |
| `sslProtocol` | 启用协议的位掩码。`24` = TLS 1.2 + TLS 1.3 |
| `ciphers` | 以冒号分隔的密码套件列表 |
| `enableECDHE` | 启用 ECDHE 密钥交换（`1` = 开启） |
| `enableDHE` | 启用 DHE 密钥交换（`0` = 关闭，建议关闭以提升性能） |

### 协议位掩码值

| 值 | 协议 |
|---|---|
| 1 | SSL 3.0（不安全，请勿使用） |
| 2 | TLS 1.0（已弃用） |
| 4 | TLS 1.1（已弃用） |
| 8 | TLS 1.2 |
| 16 | TLS 1.3 |
| 24 | TLS 1.2 + TLS 1.3（推荐） |

## Let's Encrypt 集成

### 使用 Certbot

安装 certbot 并获取证书：

```bash
# AlmaLinux / Rocky
dnf install certbot

# Ubuntu / Debian
apt install certbot
```

使用 webroot 方式获取证书。OLS 会自动提供验证文件：

```bash
certbot certonly --webroot -w /var/www/example.com/public/ -d example.com -d www.example.com
```

然后将监听器指向生成的文件：

```apacheconf
keyFile                 /etc/letsencrypt/live/example.com/privkey.pem
certFile                /etc/letsencrypt/live/example.com/fullchain.pem
```

重启 OLS：

```bash
systemctl restart lsws
```

### 自动续期

Certbot 会安装 systemd 定时器或 cron 任务来自动续期。添加续期后的部署钩子以重启 OLS：

```bash
# 创建 /etc/letsencrypt/renewal-hooks/deploy/restart-ols.sh
#!/bin/bash
systemctl restart lsws
```

```bash
chmod +x /etc/letsencrypt/renewal-hooks/deploy/restart-ols.sh
```

测试续期：

```bash
certbot renew --dry-run
```

## 按虚拟主机配置 SSL

对于基于 SNI 的多域名 SSL，可以在虚拟主机配置中或通过 WebAdmin 为每个虚拟主机配置证书。OLS 会根据请求的主机名自动选择正确的证书。

在 WebAdmin 管理界面中：
1. 进入 **Virtual Hosts** > 您的虚拟主机 > **SSL**。
2. 设置 **Private Key File** 和 **Certificate File**。
3. 保存并重启。

## HTTP 到 HTTPS 重定向

添加重写规则将所有 HTTP 流量重定向到 HTTPS。在虚拟主机配置中：

```apacheconf
rewrite {
    enable                  1
    rules                   <<<END_rules
RewriteCond %{HTTPS} !on
RewriteRule ^(.*)$ https://%{HTTP_HOST}%{REQUEST_URI} [R=301,L]
    END_rules
}
```

或在 `.htaccess` 中（使用 LiteHTTPD 模块）：

```apacheconf
RewriteEngine On
RewriteCond %{HTTPS} !on
RewriteRule ^(.*)$ https://%{HTTP_HOST}%{REQUEST_URI} [R=301,L]
```

## OCSP Stapling

启用 OCSP stapling 以加速 SSL 握手。在 WebAdmin 管理界面中：

1. 进入 **Listeners** > 您的 HTTPS 监听器 > **SSL** 标签页。
2. 将 **Enable OCSP Stapling** 设为 `Yes`。
3. 保存并重启。

## 安全建议

- 使用 `sslProtocol 24`（仅 TLS 1.2 + 1.3）。
- 禁用 DHE，使用 ECDHE 以获得更好的性能。
- 使用强密码列表。优先使用 GCM 和 ChaCha20 密码。
- 启用 HSTS 头。请参阅[安全头](/zh/ols/security-headers/)。
- 定期在证书过期前续期。

## 下一步

- [监听器](/zh/ols/listeners/) -- 监听器配置基础。
- [安全头](/zh/ols/security-headers/) -- 添加 HSTS 及其他头。
- [虚拟主机](/zh/ols/virtual-hosts/) -- 按虚拟主机配置 SSL。
