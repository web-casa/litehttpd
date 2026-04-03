# 四引擎对照测试覆盖范围

## 测试优先级说明

### P0 — 核心功能（30 个用例）
关键路径，必须通过的基础功能测试。

### P1 — 重要功能（70 个用例）
重要但非关键的功能，常见使用场景。

### P2 — 边缘场景（70 个用例）
边缘情况、压力测试、罕见场景。

---

## P0 测试用例（30 个）

### rewrite_core（8 个）
- RW_001: WordPress fallback 路由
- RW_002: 捕获组反向引用 $1
- RW_003: QSA flag — 查询字符串追加
- RW_004: NC flag — 大小写不敏感
- RW_005: RewriteCond -f — 文件存在检查
- RW_006: RewriteCond -f — PHP 文件直接服务
- RW_007: 静态文件服务
- RW_008: 查询字符串保留

### headers_core（5 个）
- HD_001: Header set — 自定义 header
- HD_002: Header always set — 安全 headers
- HD_003: Header append — 多值追加
- HD_004: Header unset — 移除 header
- HD_005: ExpiresByType — CSS 缓存控制

### security_core（6 个）
- SC_001: `<Files>` 块 ACL
- SC_002: Require all denied
- SC_003: `<FilesMatch>` .sql 文件
- SC_004: `<FilesMatch>` .log 文件
- SC_005: Options -Indexes
- SC_006: AuthType Basic 认证

### redirect_core（3 个）
- RD_001: Redirect 301
- RD_002: RedirectMatch 正则捕获
- RD_003: 非匹配路径

### module_core（5 个）
- MC_001: SetEnv 环境变量
- MC_002: php_value 配置
- MC_003: Auth 有效凭证
- MC_004: Auth 错误密码
- MC_005: SetEnvIf 条件设置

### inherit_core（3 个）
- IH_001: 子目录覆盖父级 header
- IH_002: 子目录继承父级安全 headers
- IH_003: 子目录覆盖 SetEnv

---

## P1 测试用例（70 个）

### rewrite_advanced（6 个）
- 多个 RewriteCond AND 逻辑
- RewriteCond OR flag
- RewriteRule [R=302] flag
- RewriteRule [F] flag — 403
- RewriteRule [G] flag — 410
- RewriteCond %{QUERY_STRING} 匹配

### headers_advanced（8 个）
- Header merge — 合并多值
- Header add — 多个 Set-Cookie
- Header edit — 正则替换
- Header 条件设置（env）
- RequestHeader set/unset
- Header always 在错误时应用
- 多个安全 headers 组合

### security_advanced（10 个）
- Order Deny,Allow 默认允许
- Order Allow,Deny 默认拒绝
- Allow from 特定 IP
- Deny from 特定 IP
- Require ip — Apache 2.4 语法
- Require not ip
- `<FilesMatch>` 复杂正则
- `<Files>` 嵌套在 `<Directory>`
- Options -ExecCGI
- Options +FollowSymLinks

### auth_advanced（6 个）
- 用户名大小写不敏感
- 空密码拒绝
- 密码中的特殊字符
- Require valid-user
- Require user 特定用户
- AuthName realm 显示

### expires_advanced（5 个）
- ExpiresDefault 回退
- ExpiresByType image/png
- ExpiresByType application/pdf
- Expires "now" 关键字
- 基于修改时间的 Expires

### env_advanced（6 个）
- SetEnvIf User-Agent 浏览器检测
- SetEnvIf Referer 来源检查
- BrowserMatch IE 检测
- SetEnv 特殊字符
- SetEnvIf 正则捕获
- 多个 SetEnv 顺序

### php_advanced（5 个）
- php_flag display_errors
- php_value upload_max_filesize
- php_admin_value 不可覆盖
- php_admin_flag 布尔管理设置
- 多个 php_value 指令

### error_doc_advanced（4 个）
- ErrorDocument 403 自定义消息
- ErrorDocument 404 自定义消息
- ErrorDocument 绝对 URL
- ErrorDocument 本地路径

### mime_types（5 个）
- AddType 自定义 MIME
- ForceType 强制 MIME
- AddEncoding gzip
- AddCharset UTF-8
- DefaultType 回退

### handler_advanced（3 个）
- AddHandler .php5 作为 PHP
- SetHandler 强制 handler
- RemoveHandler 禁用 handler

### limit_methods（4 个）
- `<Limit GET>` 限制 GET
- `<Limit POST>` 允许 POST
- `<LimitExcept GET POST>` 阻止其他方法
- `<LimitExcept>` 允许列出的方法

### inherit_advanced（5 个）
- 深度嵌套 3 层
- 子目录禁用父级指令
- 子目录添加到父级 headers
- 子目录覆盖父级 ACL
- 子目录继承父级 Expires

---

## P2 测试用例（70 个）

### edge_cases（10 个）
- 空 .htaccess 文件
- 超长 header 值（1KB）
- Header 特殊字符
- URI 编码字符 %20
- URI Unicode 字符
- 查询字符串特殊字符
- 超长查询字符串（2KB）
- 无 User-Agent 请求
- 路径多斜杠 ///
- 路径点段 /./

### performance（5 个）
- 100 个 headers
- 深度目录嵌套（10 层）
- 大型 .htaccess（1000 行）
- 100 个 FilesMatch 规则
- 并发请求压力测试（100 并发）

### regex_edge_cases（8 个）
- FilesMatch 锚点 ^$
- FilesMatch 字符类 [a-z]
- FilesMatch 否定 [^0-9]
- FilesMatch 量词 {2,4}
- FilesMatch 交替 (jpg|png)
- FilesMatch 前瞻 (?=pattern) ⚠️ (POSIX ERE only, no PCRE lookahead)
- FilesMatch 大小写不敏感 ⚠️ (REG_ICASE not enabled)
- RedirectMatch 复杂模式

### acl_edge_cases（6 个）
- Allow from CIDR /8
- Deny from CIDR /16
- Allow from hostname ⚠️ (not implemented — IPv4 only)
- Deny from all except localhost
- Require ip IPv6 ⚠️ (not implemented — IPv4 only)
- 复杂 ACL 多个 Allow/Deny

### brute_force（5 个）
- 第 1 次尝试允许
- 第 5 次尝试允许
- 第 6 次尝试阻止
- 白名单绕过
- X-Forwarded-For 支持

### ifmodule（4 个）
- `<IfModule mod_rewrite.c>` 启用
- `<IfModule mod_headers.c>` 启用
- `<IfModule !mod_php.c>` 否定
- `<IfModule nonexistent>` 禁用

### require_complex（5 个）
- `<RequireAll>` 所有条件
- `<RequireAny>` 任一条件
- `<RequireNone>` 无条件
- 嵌套 Require 块
- Require 环境变量

### options_complex（4 个）
- Options +Indexes 启用目录列表
- Options -Indexes +FollowSymLinks
- Options All
- Options None

### encoding_charset（3 个）
- AddDefaultCharset UTF-8 ⚠️ (not implemented — use AddCharset instead)
- AddCharset .txt 文件
- AddEncoding .br 文件
- RemoveEncoding 指令

### directory_index（3 个）
- DirectoryIndex 多个文件
- DirectoryIndex 回退顺序
- DirectoryIndex 禁用

### malformed_input（5 个）
- 畸形 Authorization header
- 超长 URI（8KB）
- URI null 字节 %00
- Header 注入尝试
- 双重 URL 编码

### cache_control（4 个）
- Cache-Control: no-cache
- Cache-Control: no-store
- Cache-Control: must-revalidate
- Pragma: no-cache

---

## 测试覆盖统计

| 优先级 | 用例数 | 指令类型 | 覆盖范围 |
|--------|--------|----------|----------|
| P0 | 30 | 核心指令 | Rewrite, Headers, Security, Redirect, Module, Inherit |
| P1 | 70 | 扩展指令 | Auth, Expires, Env, PHP, ErrorDoc, MIME, Handler, Limit |
| P2 | 70 | 边缘场景 | Edge cases, Performance, Regex, ACL, BruteForce, Complex |
| **总计** | **170** | **全面覆盖** | **所有主要 .htaccess 指令** |

---

## 运行测试

### 运行 P0 测试（默认）
```bash
cd tests/e2e/compare
./run_compare.sh
```

### 运行 P1 测试
```bash
cd tests/e2e/compare
PRIORITY=p1 ./run_compare.sh
```

### 运行 P2 测试
```bash
cd tests/e2e/compare
PRIORITY=p2 ./run_compare.sh
```

### 运行所有测试
```bash
cd tests/e2e/compare
for priority in p0 p1 p2; do
  echo "=== Running $priority tests ==="
  PRIORITY=$priority ./run_compare.sh test
done
```

### 运行特定组
```bash
PRIORITY=p1 ./run_compare.sh test --group headers_advanced
```

### 运行特定用例
```bash
PRIORITY=p1 ./run_compare.sh test --case HD_P1_001
```

---

## 已知差异

详见 `expected_diff.md`

---

## 测试报告

测试结果保存在 `out/` 目录：
- `summary.csv` — CSV 格式摘要
- `diff.json` — JSON 格式详细差异
