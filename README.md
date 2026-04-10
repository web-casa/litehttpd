# LiteHTTPD — Apache .htaccess Compatibility for OpenLiteSpeed

[简体中文](README_CN.md)

> **RPM Repository**: [rpms.litehttpd.com](https://rpms.litehttpd.com/) · **Documentation**: [docs.litehttpd.com](https://docs.litehttpd.com/) · **Source Code**: [GitHub](https://github.com/web-casa/litehttpd)

Full Apache .htaccess compatibility module for OpenLiteSpeed. 80 directives, 100% WordPress compatibility, 2.5x faster static performance than Apache.

## Quick Install

```bash
# EL 8/9/10 — one command
curl -s https://rpms.litehttpd.com/setup.sh | bash
dnf install openlitespeed-litehttpd
```

RPM includes: patched OLS binary + litehttpd module + auto-configuration. Browse packages at [rpms.litehttpd.com](https://rpms.litehttpd.com/).

## Features

- **80 .htaccess directives** — Header, Redirect, RewriteRule/RewriteCond, SetEnv, Expires, php_value/php_flag, Options, Require, AuthType Basic, DirectoryIndex, FilesMatch, and more
- **If/ElseIf/Else** conditional blocks with full **ap_expr** engine
- **RewriteOptions** inherit/IgnoreInherit and **RewriteMap** txt/rnd/int
- **IPv6 CIDR** access control with `Require ip` prefix matching
- **WordPress brute-force protection** (8 directives)
- **AllowOverride** category filtering (AuthConfig, FileInfo, Indexes, Limit, Options)
- **readApacheConf**: embedded Apache config parser in OLS binary (like CyberPanel/LSWS)
- **litehttpd-confconv**: Apache httpd.conf to OLS config converter (65+ directives)
- **Hot reload**: .htaccess changes take effect immediately — no restart needed
- **Directory cascading**: per-directory .htaccess with parent-child inheritance

## Editions

| | LiteHTTPD-Full | LiteHTTPD-Thin |
|-|----------------|---------------|
| **Install** | `dnf install openlitespeed-litehttpd` | Copy `.so` to stock OLS |
| **Directives** | 80 (all features) | 70+ (no RewriteRule execution, no php_value) |
| **Best for** | Production, full Apache migration | Quick evaluation, Docker |

## Performance

Benchmark: Linode 4C/8G, AlmaLinux 9, WordPress, PHP 8.3

| Metric | Apache 2.4 | OLS-Full | Stock OLS |
|--------|-----------|----------|-----------|
| Static RPS | 23,909 | **58,891** | 63,140 |
| PHP RPS | 274 | **292** | 258 |
| .htaccess features | 10/10 | **10/10** | 6/10 |
| Server RSS | 618 MB | **449 MB** | 320 MB |

WordPress plugin compatibility: **9/9 match Apache** (Wordfence, Yoast SEO, W3 Total Cache, WP Super Cache, All In One Security, Redirection, WPS Hide Login, Disable XML-RPC, iThemes Security, HTTP Headers).

## vs CyberPanel .htaccess Module

LiteHTTPD supports **80 directives** vs CyberPanel's ~29. All CyberPanel features are covered, plus 27 additional capabilities including RewriteRule execution, If/ElseIf/Else conditionals, ap_expr engine, Require directives, AuthType Basic, Options, AllowOverride, and readApacheConf.

## OLS Patches

4 patches extend OLS for full functionality:

| Patch | Feature |
|-------|---------|
| 0001 | PHPConfig LSIAPI (php_value/php_flag to lsphp) |
| 0002 | RewriteRule engine (parse/exec/free) |
| 0003 | Embedded Apache config parser (readApacheConf) |
| 0004 | Options -Indexes returns 403 |

## Building from Source

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
# Output: build/litehttpd_htaccess.so + build/litehttpd-confconv
```

## Testing

1036 tests across four suites:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure -j$(nproc)
```

## Project

LiteHTTPD is a sub-project of [Web.Casa](https://web.casa), an AI-native open source server control panel.

Related: [LLStack](https://llstack.com) — server management platform built on LiteHTTPD.

## License

GNU General Public License v3.0 — see [LICENSE](LICENSE) for details.
