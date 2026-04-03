---
title: "ModSecurity"
description: "Configuring ModSecurity WAF on OpenLiteSpeed with OWASP CRS"
---

## Overview

ModSecurity is a web application firewall (WAF) engine that inspects HTTP requests and responses against a set of rules. OLS includes built-in ModSecurity v3 (libmodsecurity) support -- no separate module installation is required.

## Enable ModSecurity

### Via WebAdmin GUI

1. Navigate to **Server Configuration > Security > WAF**
2. Set **Enable ModSecurity** to `Yes`
3. Save and restart OLS

### Via Configuration File

In `httpd_config.conf`:

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

## Install OWASP Core Rule Set (CRS)

The OWASP CRS provides a comprehensive set of rules that protect against common web attacks.

```bash
cd /usr/local/lsws/conf
mkdir -p modsec
cd modsec

# Download CRS
git clone https://github.com/coreruleset/coreruleset.git
cd coreruleset
cp crs-setup.conf.example crs-setup.conf
```

### Create the Main Configuration

Create `/usr/local/lsws/conf/modsec/main.conf`:

```apacheconf
# ModSecurity recommended configuration
SecRuleEngine On
SecRequestBodyAccess On
SecRequestBodyLimit 13107200
SecRequestBodyNoFilesLimit 131072
SecRequestBodyLimitAction Reject
SecResponseBodyAccess Off

# Temporary files
SecTmpDir /tmp
SecDataDir /tmp

# Audit log
SecAuditEngine RelevantOnly
SecAuditLogRelevantStatus "^(?:5|4(?!04))"
SecAuditLogParts ABIJDEFHZ
SecAuditLogType Serial
SecAuditLog /usr/local/lsws/logs/modsec_audit.log

# Debug log (disable in production)
# SecDebugLog /usr/local/lsws/logs/modsec_debug.log
# SecDebugLogLevel 0

# Load OWASP CRS
Include /usr/local/lsws/conf/modsec/coreruleset/crs-setup.conf
Include /usr/local/lsws/conf/modsec/coreruleset/rules/*.conf
```

Restart OLS:

```bash
systemctl restart lsws
```

## CRS Configuration Tuning

Edit `crs-setup.conf` to adjust the paranoia level and exclusions:

### Paranoia Level

```apacheconf
# Level 1: Basic protection (default, low false positives)
# Level 2: Moderate (recommended for most sites)
# Level 3: Strict
# Level 4: Maximum (high false positives)
SecAction "id:900000,phase:1,pass,t:none,nolog,\
  setvar:tx.paranoia_level=2"
```

### Anomaly Scoring Threshold

```apacheconf
# Lower = stricter (default inbound: 5, outbound: 4)
SecAction "id:900110,phase:1,pass,t:none,nolog,\
  setvar:tx.inbound_anomaly_score_threshold=5,\
  setvar:tx.outbound_anomaly_score_threshold=4"
```

## Rule Exclusions

False positives are common, especially with CMS applications. Create exclusion rules rather than disabling ModSecurity entirely.

### Per-Rule Exclusion

Create `/usr/local/lsws/conf/modsec/exclusions.conf`:

```apacheconf
# WordPress admin AJAX
SecRule REQUEST_URI "@beginsWith /wp-admin/admin-ajax.php" \
  "id:1001,phase:1,pass,nolog,\
  ctl:ruleRemoveById=941100-941999"

# WordPress post editor
SecRule REQUEST_URI "@beginsWith /wp-admin/post.php" \
  "id:1002,phase:1,pass,nolog,\
  ctl:ruleRemoveById=942100-942999"

# WordPress uploads
SecRule REQUEST_URI "@beginsWith /wp-admin/async-upload.php" \
  "id:1003,phase:1,pass,nolog,\
  ctl:ruleRemoveById=921110-921999"
```

Include it in `main.conf` after the CRS rules:

```apacheconf
Include /usr/local/lsws/conf/modsec/exclusions.conf
```

### Disable a Specific Rule Globally

```apacheconf
SecRuleRemoveById 920350
```

## Per-VHost ModSecurity

You can enable or disable ModSecurity per virtual host:

```apacheconf
virtualhost example {
  ...
  module mod_security {
    modsecurity             on
    modsecurity_rules       `SecRuleEngine On`
  }
}
```

To disable for a specific vhost:

```apacheconf
virtualhost staging {
  ...
  module mod_security {
    modsecurity             off
  }
}
```

## Monitoring and Logs

### Audit Log

The audit log records all requests that triggered rules:

```bash
tail -f /usr/local/lsws/logs/modsec_audit.log
```

### Identify False Positives

Search for blocked requests:

```bash
grep "ModSecurity: Access denied" /usr/local/lsws/logs/error.log
```

Each log entry includes the rule ID, which you can use to create targeted exclusions.

### Log Rotation

Add to `/etc/logrotate.d/modsecurity`:

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

## Troubleshooting

**ModSecurity blocking legitimate requests:**
- Check the audit log for the rule ID
- Add a targeted exclusion rather than disabling ModSecurity
- Lower the paranoia level if false positives are excessive

**Performance impact:**
- Set `SecResponseBodyAccess Off` unless you need response body inspection
- Limit `SecRequestBodyLimit` to a reasonable size
- Use `SecRuleEngine DetectionOnly` to log without blocking during initial deployment

**OLS not starting after enabling ModSecurity:**
- Check for syntax errors in rule files: review `/usr/local/lsws/logs/error.log`
- Validate the CRS installation path in `Include` directives
