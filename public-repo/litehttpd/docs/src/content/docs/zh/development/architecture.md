---
title: 架构
description: LiteHTTPD 模块架构与内部机制
---

## 模块概览

LiteHTTPD 是一个 LSIAPI 模块（`litehttpd_htaccess.so`），在请求生命周期的两个阶段挂载到 OpenLiteSpeed。两个版本使用相同的 `.so` 二进制文件：LiteHTTPD-Thin 运行在原生 OLS 上，提供除 RewriteRule 执行和 php_value 传递之外的所有指令。LiteHTTPD-Full 运行在打补丁的 OLS 上（通过 `openlitespeed-litehttpd` RPM），额外支持 RewriteRule 执行、php_value/php_flag、Options -Indexes 403 和 readApacheConf。模块在运行时通过对扩展 LSIAPI 函数指针的 NULL 指针检查来检测补丁可用性。

```
客户端请求
    |
    v
+-------------------+
| OLS 引擎          |
|  PROCESS_NEW_URI  |
|  VHOST_REWRITE    |
|  CONTEXT_MAP      |
+-------------------+
    |
    v
+-------------------+     +------------------+
| URI_MAP Hook      | --> | mod_htaccess.c   |
| (请求阶段)        |     |  on_uri_map()    |
+-------------------+     +------------------+
    |                           |
    |    +----------------------+
    |    | htaccess_dirwalk()
    |    | -> 解析 .htaccess 文件
    |    | -> 合并指令
    |    | -> AllowOverride 过滤
    |    +----------------------+
    |                           |
    |    执行请求阶段指令：
    |    - ACL (Order/Allow/Deny)
    |    - Auth (AuthType/Require)
    |    - Redirect/RedirectMatch
    |    - RewriteRule (通过 OLS 引擎)
    |    - PHP config (php_value/flag)
    |    - SetEnv/SetEnvIf
    |    - Options
    |    - If/ElseIf/Else 链
    |
    v
+-------------------+
| OLS 引擎          |
|  FILE_MAP         |
|  PHP 处理         |
+-------------------+
    |
    v
+-------------------+     +------------------+
| SEND_RESP_HEADER  | --> | mod_htaccess.c   |
| (响应阶段)        |     | on_send_resp()   |
+-------------------+     +------------------+
    |                           |
    |    执行响应阶段指令：
    |    - Header set/unset/append/merge
    |    - If/ElseIf/Else（响应子节点）
    |    - Expires/Cache-Control
    |    - AddType/ForceType/AddCharset
    |    - ErrorDocument
    |
    v
  响应返回客户端
```

## 源文件组织

### 核心模块

| 文件 | 用途 | 行数 |
|------|---------|-----|
| `mod_htaccess.c` | LSIAPI 入口点、钩子、指令分发 | ~1900 |
| `htaccess_parser.c` | Apache .htaccess 语法解析器 | ~2500 |
| `htaccess_dirwalker.c` | 目录遍历、合并、AllowOverride | ~1000 |
| `htaccess_cache.c` | 已解析指令的 LRU 缓存 | ~300 |
| `htaccess_directive.c` | 指令数据结构和释放 | ~200 |
| `htaccess_expr.c` | ap_expr 表达式求值器 | ~800 |
| `htaccess_printer.c` | AST 到文本的序列化 | ~400 |
| `lsiapi_shim.c` | LSIAPI 函数封装 | ~400 |

### 执行器模块（每个指令类别一个）

| 文件 | 处理的指令 |
|------|---------|
| `htaccess_exec_acl.c` | Order, Allow, Deny |
| `htaccess_exec_auth.c` | AuthType, AuthName, AuthUserFile, Require valid-user |
| `htaccess_exec_header.c` | Header, RequestHeader（所有变体） |
| `htaccess_exec_redirect.c` | Redirect, RedirectMatch, ErrorDocument |
| `htaccess_exec_rewrite.c` | RewriteEngine, RewriteRule, RewriteCond |
| `htaccess_exec_expires.c` | ExpiresActive, ExpiresByType, ExpiresDefault |
| `htaccess_exec_require.c` | Require all/ip/env, RequireAny/All |
| `htaccess_exec_env.c` | SetEnv, SetEnvIf, BrowserMatch |
| `htaccess_exec_php.c` | php_value, php_flag |
| `htaccess_exec_options.c` | Options |
| `htaccess_exec_limit.c` | Limit, LimitExcept |
| `htaccess_exec_files_match.c` | FilesMatch |
| `htaccess_exec_handler.c` | AddHandler, SetHandler（空操作） |
| `htaccess_exec_forcetype.c` | ForceType |
| `htaccess_exec_encoding.c` | AddType, AddEncoding, AddCharset |
| `htaccess_exec_brute_force.c` | LSBruteForce* |

## 缓存策略

LiteHTTPD 使用多级缓存策略：

1. **文件级缓存**（`htaccess_cache.c`）-- 已解析的指令树按文件路径 + mtime + st_blocks 缓存
2. **请求级缓存**（`req_cache`）-- 线程本地缓存，在同一请求的 URI_MAP 和 SEND_RESP_HEADER 钩子之间存储指令
3. **重写句柄缓存**（`rw_cache`）-- 线程本地缓存，用于已编译的 RewriteRule 句柄，按指令指纹索引
4. **负向 stat 缓存** -- 线程本地缓存，记录不存在的 .htaccess 路径以避免重复 stat() 调用

## 线程安全

- 所有缓存使用 `__thread`（线程本地存储）-- 无需加锁
- OLS 使用多进程、每进程单线程架构
- 模块在单个工作进程内是可重入的
