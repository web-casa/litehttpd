---
title: Architecture
description: LiteHTTPD module architecture and internals
---

## Module Overview

LiteHTTPD is an LSIAPI module (`litehttpd_htaccess.so`) that hooks into OpenLiteSpeed at two request lifecycle phases. The same `.so` binary is used in both editions: LiteHTTPD-Thin runs on stock OLS and provides all directives except RewriteRule execution and php_value passthrough. LiteHTTPD-Full runs on patched OLS (via `openlitespeed-litehttpd` RPM) and adds RewriteRule execution, php_value/php_flag, Options -Indexes 403, and readApacheConf. The module detects patch availability at runtime via NULL pointer checks on the extended LSIAPI function pointers.

```
Client Request
    |
    v
+-------------------+
| OLS Engine        |
|  PROCESS_NEW_URI  |
|  VHOST_REWRITE    |
|  CONTEXT_MAP      |
+-------------------+
    |
    v
+-------------------+     +------------------+
| URI_MAP Hook      | --> | mod_htaccess.c   |
| (Request Phase)   |     |  on_uri_map()    |
+-------------------+     +------------------+
    |                           |
    |    +----------------------+
    |    | htaccess_dirwalk()
    |    | -> parse .htaccess files
    |    | -> merge directives
    |    | -> AllowOverride filter
    |    +----------------------+
    |                           |
    |    Execute request-phase directives:
    |    - ACL (Order/Allow/Deny)
    |    - Auth (AuthType/Require)
    |    - Redirect/RedirectMatch
    |    - RewriteRule (via OLS engine)
    |    - PHP config (php_value/flag)
    |    - SetEnv/SetEnvIf
    |    - Options
    |    - If/ElseIf/Else chains
    |
    v
+-------------------+
| OLS Engine        |
|  FILE_MAP         |
|  PHP Processing   |
+-------------------+
    |
    v
+-------------------+     +------------------+
| SEND_RESP_HEADER  | --> | mod_htaccess.c   |
| (Response Phase)  |     | on_send_resp()   |
+-------------------+     +------------------+
    |                           |
    |    Execute response-phase directives:
    |    - Header set/unset/append/merge
    |    - If/ElseIf/Else (response children)
    |    - Expires/Cache-Control
    |    - AddType/ForceType/AddCharset
    |    - ErrorDocument
    |
    v
  Response to Client
```

## Source File Organization

### Core Module

| File | Purpose | LOC |
|------|---------|-----|
| `mod_htaccess.c` | LSIAPI entry point, hooks, directive dispatch | ~1900 |
| `htaccess_parser.c` | Apache .htaccess syntax parser | ~2500 |
| `htaccess_dirwalker.c` | Directory walk, merge, AllowOverride | ~1000 |
| `htaccess_cache.c` | LRU cache for parsed directives | ~300 |
| `htaccess_directive.c` | Directive data structures and free | ~200 |
| `htaccess_expr.c` | ap_expr expression evaluator | ~800 |
| `htaccess_printer.c` | AST to text serialization | ~400 |
| `lsiapi_shim.c` | LSIAPI function wrappers | ~400 |

### Executor Modules (one per directive category)

| File | Handles |
|------|---------|
| `htaccess_exec_acl.c` | Order, Allow, Deny |
| `htaccess_exec_auth.c` | AuthType, AuthName, AuthUserFile, Require valid-user |
| `htaccess_exec_header.c` | Header, RequestHeader (all variants) |
| `htaccess_exec_redirect.c` | Redirect, RedirectMatch, ErrorDocument |
| `htaccess_exec_rewrite.c` | RewriteEngine, RewriteRule, RewriteCond |
| `htaccess_exec_expires.c` | ExpiresActive, ExpiresByType, ExpiresDefault |
| `htaccess_exec_require.c` | Require all/ip/env, RequireAny/All |
| `htaccess_exec_env.c` | SetEnv, SetEnvIf, BrowserMatch |
| `htaccess_exec_php.c` | php_value, php_flag |
| `htaccess_exec_options.c` | Options |
| `htaccess_exec_limit.c` | Limit, LimitExcept |
| `htaccess_exec_files_match.c` | FilesMatch |
| `htaccess_exec_handler.c` | AddHandler, SetHandler (no-op) |
| `htaccess_exec_forcetype.c` | ForceType |
| `htaccess_exec_encoding.c` | AddType, AddEncoding, AddCharset |
| `htaccess_exec_brute_force.c` | LSBruteForce* |

## Caching Strategy

LiteHTTPD uses a multi-level caching strategy:

1. **File-level cache** (`htaccess_cache.c`) -- parsed directive trees cached by file path + mtime + st_blocks
2. **Request-level cache** (`req_cache`) -- thread-local cache storing directives between URI_MAP and SEND_RESP_HEADER hooks for the same request
3. **Rewrite handle cache** (`rw_cache`) -- thread-local cache for compiled RewriteRule handles, keyed by directive fingerprint
4. **Negative stat cache** -- thread-local cache of non-existent .htaccess paths to avoid repeated stat() calls

## Thread Safety

- All caches use `__thread` (thread-local storage) -- no locks needed
- OLS uses multi-process, single-thread-per-process architecture
- The module is reentrant within a single worker process
