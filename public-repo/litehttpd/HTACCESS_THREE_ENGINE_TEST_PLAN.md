# OLS `.htaccess` 三引擎对照测试方案（优化版）

> 基于原始方案优化，修正了用例覆盖范围，补充了模块核心功能测试。

---

## 1. 目标与范围

对以下三种引擎进行功能一致性对照验证：

1. **Apache 2.4**（黄金对照组）
2. **OLS 原生**（不加载 `litehttpd_htaccess.so`，负对照）
3. **OLS + `litehttpd_htaccess.so`**（被测对象）

重点：
- 验证 OLS+module 与 Apache 的行为一致性
- 明确 OLS 原生 rewrite 引擎与 Apache mod_rewrite 的差异
- 验证模块独有功能（子目录继承、Header、ACL、Auth、PHP 配置传递）
- 输出可重复的自动化对照报告

---

## 2. 能力边界（必须遵守）

| 功能类别 | 处理引擎 | 说明 |
|----------|---------|------|
| RewriteEngine/Rule/Cond/Base | OLS 原生 | 不经过模块，三引擎应接近一致 |
| Header/RequestHeader | litehttpd_htaccess 模块 | Apache ≈ OLS+module，OLS native 无此功能 |
| Expires/Cache-Control | litehttpd_htaccess 模块 | 同上 |
| Order/Allow/Deny/Require | litehttpd_htaccess 模块 | 同上 |
| Files/FilesMatch | litehttpd_htaccess 模块 | PHP/静态文件有已知限制 |
| AuthType Basic | litehttpd_htaccess 模块 | 同上 |
| SetEnv/SetEnvIf/BrowserMatch | litehttpd_htaccess 模块 | 同上 |
| php_value/php_flag | litehttpd_htaccess 模块 | 同上 |
| Options/DirectoryIndex | litehttpd_htaccess 模块 | 同上 |
| Redirect/RedirectMatch | litehttpd_htaccess 模块 | 同上 |
| ErrorDocument | litehttpd_htaccess 模块 | 同上 |
| 子目录 .htaccess 继承 | litehttpd_htaccess DirWalker | Apache 原生支持，OLS native 不支持 |

---

## 3. 测试环境

### 3.1 服务定义

| 服务 | 端口 | 说明 |
|------|------|------|
| `apache24` | `http://localhost:18080` | Apache 2.4 + PHP 8.1 (mod_php) |
| `ols_native` | `http://localhost:28080` | OLS，不加载 litehttpd_htaccess.so |
| `ols_module` | `http://localhost:38080` | OLS，加载 litehttpd_htaccess.so |

### 3.2 一致性要求

- 三引擎共享同一份 `htdocs/` 文档根目录
- 同一批 `.htaccess` 用例文件
- 同一个 `probe.php` 探针脚本

### 3.3 启动方式

```bash
cd tests/e2e/compare

# 完整流程：编译 → 启动 → 测试 → 清理
./run_compare.sh

# 分步执行
./run_compare.sh up          # 启动三引擎
./run_compare.sh test        # 运行测试
./run_compare.sh test --group headers_core  # 只跑某组
./run_compare.sh test --case HD_001         # 只跑某条
./run_compare.sh down        # 停止清理
```

---

## 4. 目录结构

```text
tests/e2e/compare/
├── docker-compose.compare.yml   # 三引擎编排
├── Dockerfile.apache            # Apache 2.4
├── Dockerfile.ols-native        # OLS 无模块
├── Dockerfile.ols-module        # OLS + 模块
├── conf/
│   ├── vhconf_compare.conf      # OLS vhost 配置
│   ├── httpd_compare_native.conf
│   └── httpd_compare_module.conf
├── htdocs/                      # 共享文档根目录
│   ├── .htaccess                # 根级规则（rewrite + header + acl + ...）
│   ├── _probe/probe.php         # 探针脚本
│   ├── subdir/                  # 子目录继承测试
│   │   ├── .htaccess            # 覆盖父级指令
│   │   └── _probe/probe.php
│   ├── protected/               # Auth Basic 测试
│   │   ├── .htaccess
│   │   ├── .htpasswd
│   │   └── secret.php
│   └── noindex/                 # Options -Indexes 测试
├── cases/
│   └── p0_cases.yaml            # 30 条 P0 用例定义
├── compare_runner.sh            # 自动化测试脚本
├── run_compare.sh               # 一键启动脚本
├── expected_diff.md             # 已知差异文档
└── out/                         # 测试输出
    ├── summary.csv
    └── diff.json
```

---

## 5. 探针脚本（`_probe/probe.php`）

输出稳定 JSON，包含：

- 服务器变量：REQUEST_URI, QUERY_STRING, SCRIPT_NAME, REQUEST_METHOD 等
- 环境变量：TEST_ENV, CUSTOM_VAR, BROWSER_FLAG（验证 SetEnv/SetEnvIf）
- PHP 配置：memory_limit, display_errors, max_execution_time（验证 php_value）
- GET/POST 数据
- 标记：`PROBE_V2`

---

## 6. 对比规则

### 6.1 compare_mode 定义

| 模式 | 含义 | 判定 |
|------|------|------|
| `all3` | 三引擎行为应一致 | apache ≈ ols_native ≈ ols_module |
| `mod` | 模块指令，OLS native 作为负对照 | apache ≈ ols_module |
| `known` | 已知差异 | 仅验证 apache，module 差异记录但不判定失败 |

### 6.2 必比字段

- HTTP 状态码
- Location 头（重定向）
- 关键响应头（白名单）
- probe JSON 字段
- body 关键标记

### 6.3 归一化（忽略）

- Date, Server, X-Litespeed-* 等动态头
- Set-Cookie 随机值

---

## 7. P0 用例清单（30 条）

### 7.1 rewrite_core（8 条）— compare_mode: all3

| ID | 描述 |
|----|------|
| RW_001 | WordPress 风格 fallback：不存在路径 → index.php |
| RW_002 | 捕获组 backref $1 |
| RW_003 | QSA — 追加查询字符串 |
| RW_004 | NC — 大小写不敏感匹配 |
| RW_005 | RewriteCond -f：已存在文件绕过 rewrite |
| RW_006 | 已存在 PHP 文件直接服务 |
| RW_007 | 静态文件直接服务 |
| RW_008 | 不存在路径 + 查询字符串保留 |

### 7.2 headers_core（5 条）— compare_mode: mod

| ID | 描述 |
|----|------|
| HD_001 | Header set — 自定义头存在 |
| HD_002 | Header always set — 安全头 |
| HD_003 | Header append — 多值追加 |
| HD_004 | Header unset — X-Powered-By 移除 |
| HD_005 | ExpiresByType — CSS 获得 Cache-Control |

### 7.3 security_core（6 条）

| ID | 描述 | compare_mode |
|----|------|-------------|
| SC_001 | `<Files wp-config.php>` 403 | mod |
| SC_002 | `<Files secret.txt>` Require all denied | mod |
| SC_003 | `<FilesMatch .sql>` 403 | known |
| SC_004 | `<FilesMatch .log>` 403 | known |
| SC_005 | Options -Indexes 禁止目录列表 | mod |
| SC_006 | AuthType Basic 无凭据返回 401 | mod |

### 7.4 redirect_core（3 条）

| ID | 描述 | compare_mode |
|----|------|-------------|
| RD_001 | Redirect 301 /old-page → /new-page | mod |
| RD_002 | RedirectMatch 带捕获组 | mod |
| RD_003 | 不匹配的路径不触发重定向 | all3 |

### 7.5 module_core（5 条）— compare_mode: mod

替代原方案的 cache_core，测试模块核心功能：

| ID | 描述 |
|----|------|
| MC_001 | SetEnv — 环境变量在 probe 中可见 |
| MC_002 | php_value memory_limit — ini_get 反映 |
| MC_003 | AuthType Basic — 正确凭据通过 |
| MC_004 | AuthType Basic — 错误密码拒绝 |
| MC_005 | SetEnvIf Remote_Addr 条件匹配 |

### 7.6 inherit_core（3 条）— compare_mode: mod

测试子目录 .htaccess 继承（DirWalker 核心价值）：

| ID | 描述 |
|----|------|
| IH_001 | 子目录覆盖父级 Header |
| IH_002 | 子目录继承父级安全头 |
| IH_003 | 子目录覆盖父级 SetEnv |

---

## 8. 与原方案的主要差异

| 原方案 | 优化后 | 原因 |
|--------|--------|------|
| cache_core 5 条（LSCache 页面缓存） | module_core 5 条（SetEnv/Auth/php_value） | LSCache 不是模块功能，无法三引擎对比 |
| HTTP→HTTPS 301 在 redirect_core | 移除 | 这是 RewriteCond %{HTTPS}，属于 OLS 原生 rewrite |
| 重定向循环保护 | 替换为不匹配路径测试 | 模块 Redirect 是单次匹配，不存在循环 |
| 无子目录继承测试 | 新增 inherit_core 3 条 | DirWalker 是模块核心价值 |
| 无 Auth 测试 | 新增 MC_003/MC_004 | AuthType Basic 是重要功能 |
| 无 PHP 配置测试 | 新增 MC_002 | php_value 是常用功能 |
| rewrite_core 12 条 | 精简为 8 条 | 去掉 QSD/END/S=n 等 OLS 可能不支持的边缘 flag |

---

## 9. 已知风险

详见 `expected_diff.md`，主要包括：

1. `<FilesMatch>` 对静态文件/PHP 的 ACL 在 OLS 下不生效
2. ErrorDocument 文本消息格式可能不同
3. Options -Indexes 返回码可能是 403 或 404
4. Docker 网络中 Remote_Addr 不是 127.0.0.1

---

## 10. 验收标准

1. 三引擎环境可通过 `./run_compare.sh up` 一键启动
2. 30 条 P0 用例全部执行
3. `out/summary.csv` + `out/diff.json` 自动生成
4. 非预期差异为 0（known 差异已在 expected_diff.md 中标注）
5. all3 模式用例三引擎一致
6. mod 模式用例 Apache ≈ OLS+module
