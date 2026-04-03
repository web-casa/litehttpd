---
title: "ModSecurity"
description: "在 OpenLiteSpeed 上配置 ModSecurity WAF 与 OWASP CRS"
---

## 概述

ModSecurity 是一个 Web 应用防火墙（WAF）引擎，根据规则集检查 HTTP 请求和响应。OLS 内置 ModSecurity v3（libmodsecurity）支持，无需单独安装模块。

## 启用 ModSecurity

### 通过 WebAdmin 界面

1. 导航至 **服务器配置 > 安全 > WAF**
2. 将 **启用 ModSecurity** 设为 `Yes`
3. 保存并重启 OLS

### 通过配置文件

在 `httpd_config.conf` 中：

```apacheconf
module mod_security {
  ls_enabled              1
  modsecurity             on
  modsecurity_rules       `
    SecRuleEngine On
    SecRequestBodyAccess On
    SecResponseBodyAccess Off
    SecRequestBodyLimit 13107200
    SecRequestBodyNoFilesLimit 131072
  `
  modsecurity_rules_file  /usr/local/lsws/conf/modsec/main.conf
}
```

## 安装 OWASP Core Rule Set (CRS)

OWASP CRS 提供了一套全面的规则集，可防御常见的 Web 攻击。

```bash
cd /usr/local/lsws/conf
mkdir -p modsec
cd modsec

# 下载 CRS
git clone https://github.com/coreruleset/coreruleset.git
cd coreruleset
cp crs-setup.conf.example crs-setup.conf
```

### 创建主配置文件

创建 `/usr/local/lsws/conf/modsec/main.conf`：

```apacheconf
# ModSecurity 推荐配置
SecRuleEngine On
SecRequestBodyAccess On
SecRequestBodyLimit 13107200
SecRequestBodyNoFilesLimit 131072
SecRequestBodyLimitAction Reject
SecResponseBodyAccess Off

# 临时文件
SecTmpDir /tmp
SecDataDir /tmp

# 审计日志
SecAuditEngine RelevantOnly
SecAuditLogRelevantStatus "^(?:5|4(?!04))"
SecAuditLogParts ABIJDEFHZ
SecAuditLogType Serial
SecAuditLog /usr/local/lsws/logs/modsec_audit.log

# 调试日志（生产环境中禁用）
# SecDebugLog /usr/local/lsws/logs/modsec_debug.log
# SecDebugLogLevel 0

# 加载 OWASP CRS
Include /usr/local/lsws/conf/modsec/coreruleset/crs-setup.conf
Include /usr/local/lsws/conf/modsec/coreruleset/rules/*.conf
```

重启 OLS：

```bash
systemctl restart lsws
```

## CRS 配置调优

编辑 `crs-setup.conf` 调整偏执等级和排除规则：

### 偏执等级

```apacheconf
# 等级 1：基础保护（默认，误报率低）
# 等级 2：中等保护（推荐大多数站点使用）
# 等级 3：严格保护
# 等级 4：最大保护（误报率高）
SecAction "id:900000,phase:1,pass,t:none,nolog,\
  setvar:tx.paranoia_level=2"
```

### 异常评分阈值

```apacheconf
# 越低越严格（默认入站：5，出站：4）
SecAction "id:900110,phase:1,pass,t:none,nolog,\
  setvar:tx.inbound_anomaly_score_threshold=5,\
  setvar:tx.outbound_anomaly_score_threshold=4"
```

## 规则排除

误报很常见，尤其是在 CMS 应用中。应创建排除规则，而不是完全禁用 ModSecurity。

### 按规则排除

创建 `/usr/local/lsws/conf/modsec/exclusions.conf`：

```apacheconf
# WordPress 管理后台 AJAX
SecRule REQUEST_URI "@beginsWith /wp-admin/admin-ajax.php" \
  "id:1001,phase:1,pass,nolog,\
  ctl:ruleRemoveById=941100-941999"

# WordPress 文章编辑器
SecRule REQUEST_URI "@beginsWith /wp-admin/post.php" \
  "id:1002,phase:1,pass,nolog,\
  ctl:ruleRemoveById=942100-942999"

# WordPress 上传
SecRule REQUEST_URI "@beginsWith /wp-admin/async-upload.php" \
  "id:1003,phase:1,pass,nolog,\
  ctl:ruleRemoveById=921110-921999"
```

在 `main.conf` 中 CRS 规则之后引入该文件：

```apacheconf
Include /usr/local/lsws/conf/modsec/exclusions.conf
```

### 全局禁用特定规则

```apacheconf
SecRuleRemoveById 920350
```

## 按虚拟主机配置 ModSecurity

可以为每个虚拟主机单独启用或禁用 ModSecurity：

```apacheconf
virtualhost example {
  ...
  module mod_security {
    modsecurity             on
    modsecurity_rules       `SecRuleEngine On`
  }
}
```

为特定虚拟主机禁用：

```apacheconf
virtualhost staging {
  ...
  module mod_security {
    modsecurity             off
  }
}
```

## 监控与日志

### 审计日志

审计日志记录所有触发规则的请求：

```bash
tail -f /usr/local/lsws/logs/modsec_audit.log
```

### 识别误报

搜索被阻止的请求：

```bash
grep "ModSecurity: Access denied" /usr/local/lsws/logs/error.log
```

每条日志记录包含规则 ID，可用于创建有针对性的排除规则。

### 日志轮转

添加到 `/etc/logrotate.d/modsecurity`：

```
/usr/local/lsws/logs/modsec_audit.log {
    daily
    rotate 14
    compress
    missingok
    notifempty
    postrotate
        systemctl reload lsws
    endscript
}
```

## 故障排除

**ModSecurity 阻止了合法请求：**
- 检查审计日志中的规则 ID
- 添加有针对性的排除规则，而不是禁用 ModSecurity
- 如果误报过多，降低偏执等级

**性能影响：**
- 将 `SecResponseBodyAccess` 设为 `Off`，除非需要检查响应体
- 将 `SecRequestBodyLimit` 限制为合理大小
- 初始部署阶段使用 `SecRuleEngine DetectionOnly` 仅记录不阻止

**启用 ModSecurity 后 OLS 无法启动：**
- 检查规则文件是否有语法错误：查看 `/usr/local/lsws/logs/error.log`
- 验证 `Include` 指令中 CRS 的安装路径
