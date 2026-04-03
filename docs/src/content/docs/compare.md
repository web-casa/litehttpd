---
title: Compare Engines
description: Feature-by-feature comparison of Apache, Stock OLS, LiteHTTPD, and LSWS Enterprise
---

## At a Glance

| | Apache httpd | Stock OLS | LiteHTTPD | LSWS Enterprise |
|-|-------------|-----------|-----------|-----------------|
| **Price** | Free | Free | **Free** | From $0 (1 domain) |
| **License** | Apache 2.0 | GPLv3 | **GPLv3** | Proprietary |
| **Source Code** | Open | Open | **Open** | Closed |
| **.htaccess Support** | Full | Partial | **90%+** | Full |
| **Architecture** | Process-per-connection | Event-driven | **Event-driven** | Event-driven |

---

## .htaccess Directive Support

| Directive Category | Apache | Stock OLS | LiteHTTPD | LSWS |
|-------------------|--------|-----------|-----------|------|
| Header set/unset/append | Yes | No | **Yes** | Yes |
| Require all denied | Yes | No (returns 200) | **Yes** | Yes |
| Options -Indexes | Yes | No (returns 404) | **Yes** | Yes |
| FilesMatch ACL | Yes | No | **Yes** | Yes |
| AuthType Basic | Yes | No | **Yes** | Yes |
| RewriteRule [R=301] | Yes | No (returns 404) | **Yes** | Yes |
| ExpiresByType | Yes | Partial | **Yes** | Yes |
| If/ElseIf/Else | Yes | No | **Yes** | Yes |
| php_value/php_flag | Yes | No | **Yes** (patch) | Yes |
| ErrorDocument | Yes | No | **Yes** | Yes |
| SetEnv/SetEnvIf | Yes | No | **Yes** | Yes |
| AddType/ForceType | Yes | Partial | **Yes** | Yes |

:::note
Stock OLS processes `.htaccess` files for RewriteRule only (via `RewriteFile`). It does not execute ACL, Header, Expires, or authentication directives. This means security-critical rules like `Require all denied` silently fail (return 200 instead of 403).
:::

---

## Performance (4 vCPU, 8 GB RAM, AlmaLinux 9)

### Static File Throughput (Requests/sec)

| Scenario | Apache | LiteHTTPD | Stock OLS | LSWS |
|----------|--------|-----------|-----------|------|
| No .htaccess | 11,082 | **22,104** | 23,242 | 24,786 |
| Simple .htaccess (4 lines) | 13,020 | **37,038** | 75,908 | 83,779 |
| Complex .htaccess (~20 lines) | 12,700 | **34,115** | 80,413 | 78,458 |
| WordPress .htaccess (~200 lines) | 10,618 | **21,960** | 18,883 | 20,306 |

:::caution
Stock OLS and LSWS show higher RPS on simple/complex .htaccess because they do not actually execute the ACL/Header directives. Their security rules silently fail.
:::

### .htaccess Parsing Overhead

| Engine | No .htaccess | 200-line .htaccess | Overhead |
|--------|-------------|-------------------|----------|
| Apache | 11,082 | 10,618 | **-4.2%** |
| LiteHTTPD | 22,104 | 21,960 | **-0.7%** |
| Stock OLS | 23,242 | 18,883 | -18.8% |
| LSWS | 24,786 | 20,306 | -18.1% |

### Resource Usage

| Metric | Apache | LiteHTTPD | Stock OLS | LSWS |
|--------|--------|-----------|-----------|------|
| Baseline memory | 969 MB | **676 MB** | 663 MB | 819 MB |
| Module overhead | -- | **+13 MB** | -- | +156 MB |
| Peak CPU (static) | 98.5% | **66.0%** | 48.1% | 38.8% |
| RPS per CPU% | 525 | **1,324** | 1,890 | 1,265 |

---

## Security Feature Comparison

| Feature | Apache | Stock OLS | LiteHTTPD | LSWS |
|---------|--------|-----------|-----------|------|
| .htaccess ACL enforcement | Yes | **No** | Yes | Yes |
| .htpasswd protection | Yes | No | Yes | Yes |
| FilesMatch deny | Yes | **No (200)** | Yes (403) | Yes |
| Directory listing control | Yes | No | Yes | Yes |
| Path traversal defense | Yes | OLS engine | **Module + OLS** | Yes |
| ModSecurity | mod_security | OLS module | OLS module | Built-in |
| Anti-DDoS | External | Limited | Via OLS | Built-in |
| Brute force protection | External | No | **Built-in** | Built-in |

---

## When to Choose Each Engine

| Use Case | Recommended |
|----------|------------|
| Maximum Apache compatibility, existing infrastructure | Apache httpd |
| High performance with full .htaccess, free license | **LiteHTTPD** |
| Minimal OLS without .htaccess needs | Stock OLS |
| Enterprise support, cPanel/Plesk integration | LSWS Enterprise |
| Budget-conscious WordPress hosting | **LiteHTTPD** |
| Migrating from Apache to OLS | **LiteHTTPD** |
| Replacing paid LSWS license | **LiteHTTPD** |
| Docker/container environments where OLS binary cannot be replaced | **LiteHTTPD-Thin** |

:::note
LiteHTTPD data in the tables above represents the Full edition (installed via `openlitespeed-litehttpd` RPM). LiteHTTPD-Thin provides the same performance but without RewriteRule execution or php_value support, as it runs on stock OLS without patches.
:::
