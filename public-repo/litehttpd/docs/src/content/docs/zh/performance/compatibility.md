---
title: 兼容性测试结果
description: 4 种引擎的 .htaccess 兼容性测试结果
---

## WordPress + 19 个插件测试

| 测试 | Apache | LiteHTTPD | Stock OLS | LSWS |
|------|--------|-----------|-----------|------|
| 首页 (200) | 200 | 200 | 404 | 200 |
| wp-login.php | 200 | 200 | 200 | 200 |
| wp-admin 重定向 | 302 | 302 | 404 | 302 |
| .htaccess 保护 | 403 | 403 | 403 | 403 |
| .htpasswd 保护 | 403 | 403 | 404 | 403 |
| Options -Indexes | 403 | 403 | 404 | 403 |
| Require all denied | 403 | 403 | 200 | 403 |
| FilesMatch 拒绝 .bak | 403 | 403 | 200 | 403 |
| RewriteRule [R=301] | 301 | 301 | 404 | 301 |
| Header set | 是 | 是 | 否 | 是 |
| Cache-Control | 是 | 是 | 否 | 是 |
| AddCharset | 是 | 是 | 否 | 是 |

:::note
上表中的 "LiteHTTPD" 指的是应用了全部 OLS 补丁的 Full 版本。LiteHTTPD-Thin 在 ACL、Header 和 Expires 测试中可获得相同结果，但 RewriteRule 测试需要 Full 版本。
:::

## 兼容率

| 引擎 | 匹配数 | 比率 |
|--------|-------|------|
| **LiteHTTPD** | **18/18** | **90%+** |
| LSWS Enterprise 6.3.5 | 18/18 | 100% |
| Stock OLS 1.8.5 | 8/18 | 44% |
