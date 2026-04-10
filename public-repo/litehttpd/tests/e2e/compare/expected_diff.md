# 四引擎对照测试 — 已知差异清单

## 引擎说明

| 缩写 | 全称 | 说明 |
|------|------|------|
| Apache | Apache 2.4 httpd | 参考标准，完整 .htaccess 支持 |
| OLS Native | OpenLiteSpeed (无 module) | 仅 OLS 原生 rewrite 引擎 |
| OLS Module | OpenLiteSpeed + litehttpd_htaccess.so | 通过 LSIAPI module 处理 .htaccess |
| LSWS | LiteSpeed Enterprise (trial) | 原生 .htaccess 支持（含 rewrite） |

## 1. RewriteRule 从 .htaccess 读取 (RW_001 ~ RW_008)

**现象：** Apache 和 LSWS 正常处理 .htaccess 中的 RewriteRule，OLS Native 和 OLS Module 返回 404。

**原因：** OLS 不从 .htaccess 读取 RewriteRule — OLS 的 rewrite 引擎需要在 vhconf.conf 中配置。
litehttpd_htaccess module 的 parser 会跳过 Rewrite 指令（设计如此，rewrite 由 OLS 原生引擎处理）。

**解决方案：** 在 OLS 的 vhconf.conf 中配置 rewrite rules，或使用 LSWS Enterprise。

---

## 2. `<FilesMatch>` 对静态文件扩展名的 ACL (SC_003, SC_004)

**现象：** Apache 和 LSWS 返回 403，OLS (native + module) 可能返回 200 或 404。

**原因：** OLS 对已知静态文件扩展名（.sql, .log, .bak 等）直接提供服务，
不经过 LSIAPI module hook。模块的 `<FilesMatch>` ACL 无法拦截这些请求。

**解决方案：** 使用 OLS vhconf.conf 的 Context 配置限制敏感文件访问。

---

## 3. php_value 在 LSPHP 中的传播 (MC_002)

**现象：** Apache 的 `php_value memory_limit 256M` 生效，
OLS Module 中 LSPHP 仍显示默认值（如 1024M）。

**原因：** OLS Module 通过 `lsi_session_set_php_ini()` 设置 PHP INI，
但 LSPHP 的 LSAPI 接口可能不支持运行时修改某些 INI 值。
LSWS Enterprise 原生支持 .htaccess 中的 php_value。

**解决方案：** 使用 LSWS Enterprise，或在 OLS 的 vhconf.conf 中通过
`phpIniOverride` 设置 PHP 配置。

---

## 4. Header unset X-Powered-By (HD_004)

**现象：** Apache 的 X-Powered-By 由 PHP 设置，`Header unset` 可移除。
OLS/LSWS 可能不设置 X-Powered-By，或由 LSPHP 在不同阶段设置。

**影响：** 低。该 header 在 OLS/LSWS 下可能本来就不存在。

---

## 5. Options -Indexes 返回码 (SC_005)

**现象：** Apache 对禁止目录列表返回 403，OLS/LSWS 可能返回 403 或 404。

**影响：** 低。两者都阻止了目录浏览，状态码差异可接受。

---

## 6. Redirect 指令与 RewriteRule 的交互 (RD_001, RD_002)

**现象：** Apache 中 mod_alias (Redirect) 优先于 mod_rewrite (RewriteRule)。
OLS Module 中 Redirect 由 module 处理，但 OLS native rewrite 不从 .htaccess 读取，
所以不会产生冲突。LSWS 原生处理两者。

**影响：** 中。需要确保 .htaccess 中 Redirect 和 RewriteRule 的顺序正确。
