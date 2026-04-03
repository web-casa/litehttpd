# LiteHTTPD -- Apache .htaccess Compatibility for OpenLiteSpeed

Apache .htaccess compatibility module for OpenLiteSpeed, plus the `litehttpd-confconv` Apache-to-OLS config converter.

## Editions

| | LiteHTTPD-Thin | LiteHTTPD-Full |
|-|---------------|----------------|
| **What** | `litehttpd_htaccess.so` module only | Module + Patched OLS (4 patches) |
| **Install on** | Stock (unmodified) OLS | Custom-built OLS |
| **Install time** | 1 minute | 10-15 minutes |
| **Directives** | 70+ (security, headers, caching, conditionals, redirects) | **80 (adds RewriteRule execution, php_value/flag)** |
| **Best for** | Quick .htaccess support, Docker, evaluation | Full Apache migration, WordPress with advanced plugins |

**LiteHTTPD-Thin** works on any stock OLS -- just copy the `.so` and restart. Most security, header, and caching directives work immediately. RewriteRule and php_value are parsed but not executed.

**LiteHTTPD-Full** requires compiling OLS with 4 patches. This enables full RewriteRule execution (with [R=301,L,QSA] flags), php_value/php_flag passed to lsphp, and engine-level Options -Indexes.

## Features

- **80 .htaccess directives** -- Header, Redirect, RewriteRule/RewriteCond, SetEnv, Expires, php\_value/php\_flag, Options, Require, AuthType Basic, DirectoryIndex, FilesMatch, and more
- **If/ElseIf/Else** conditional blocks with full **ap\_expr** engine (`&&`, `||`, `!`, `-f`, `-d`, `-ipmatch`, regex, functions)
- **RewriteOptions** inherit/IgnoreInherit and **RewriteMap** txt/rnd/int (Full only)
- **IPv6 CIDR** access control with `Require ip` prefix matching and `Require env`
- **RemoveType**, **RemoveHandler**, **Action** directives
- **WordPress uploads PHP protection** and **brute-force protection** (8 directives)
- **AllowOverride** category filtering (AuthConfig, FileInfo, Indexes, Limit, Options)
- **HTACCESS\_VHROOT** three-level `.htaccess` merging
- **litehttpd-confconv**: Apache `httpd.conf` to OLS config converter (60+ directives)

## Quick Start

### LiteHTTPD-Full (RPM, EL 9)

```bash
# Install patched OLS + module
dnf install ./openlitespeed-litehttpd-*.$(uname -m).rpm
dnf install ./litehttpd-*.$(uname -m).rpm
systemctl restart lsws
```

### LiteHTTPD-Thin (any OLS)

```bash
# Copy module to stock OLS
cp litehttpd_htaccess.so /usr/local/lsws/modules/

# Enable module
cat >> /usr/local/lsws/conf/httpd_config.conf <<'EOF'

module litehttpd_htaccess {
    ls_enabled              1
}
EOF

# Restart OLS
/usr/local/lsws/bin/lswsctrl restart
```

## litehttpd-confconv: Apache to OLS Config Converter

Convert Apache `httpd.conf` / virtual host configurations to OpenLiteSpeed format:

```bash
# Convert a single Apache config
litehttpd-confconv /etc/httpd/conf/httpd.conf -o /usr/local/lsws/conf/httpd_config.conf

# Preview changes without writing
litehttpd-confconv /etc/httpd/conf/httpd.conf --dry-run
```

## OLS Patches

Three patches extend the OLS LSIAPI for full functionality:

| Patch | Description |
|-------|-------------|
| `0001-lsiapi-phpconfig.patch` | PHPConfig + `set_req_header` LSIAPI extensions |
| `0002-lsiapi-rewrite.patch` | RewriteRule engine LSIAPI extensions |
| `0003-readapacheconf.patch` | `readApacheConf` startup integration |

Apply to OLS source before building:

```bash
cd openlitespeed-1.8.x/
patch -p1 < 0001-lsiapi-phpconfig.patch
patch -p1 < 0002-lsiapi-rewrite.patch
patch -p1 < 0003-readapacheconf.patch
```

Without patches, the module still works with stock OLS -- `php_value`/`php_flag` will use environment variable fallback, and RewriteRule will be parsed but not executed.

## Building from Source

### Requirements

- CMake >= 3.14
- GCC or Clang with C11 support
- libcrypt-dev / libxcrypt-devel

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Output:
#   build/litehttpd_htaccess.so   -- the module
#   build/litehttpd-confconv      -- the config converter
```

### Build without tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build -j$(nproc)
```

## Testing

The project includes 1017 tests across four test suites:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Run all tests
ctest --test-dir build --output-on-failure -j$(nproc)

# Or run individually
build/tests/unit_tests        # 786 unit tests
build/tests/property_tests    # 124 property-based tests (RapidCheck)
build/tests/compat_tests      # 52 Apache compatibility tests
build/tests/confconv_tests    # 55 config converter tests
```

### Consistency check

Verifies all 80 directive types are covered across parser, printer, executor, dirwalker, free, and generator:

```bash
bash tests/check_consistency.sh
```

### Sanitizers

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

## Supported Directives

<details>
<summary>Full list of 80 supported directives</summary>

**Headers**: Header set/unset/append/merge/add/edit/edit\*, Header always set/unset/append/merge/add/edit/edit\*, RequestHeader set/unset

**PHP**: php\_value, php\_flag, php\_admin\_value, php\_admin\_flag

**Access Control**: Order, Allow/Deny from, Require all granted/denied, Require ip, Require not ip, Require env, Require valid-user, RequireAny, RequireAll, Satisfy

**Authentication**: AuthType, AuthName, AuthUserFile

**Redirects**: Redirect, RedirectMatch, ErrorDocument

**Rewrite**: RewriteEngine, RewriteBase, RewriteCond, RewriteRule, RewriteOptions, RewriteMap

**Conditionals**: If, ElseIf, Else, IfModule

**Content**: AddType, AddEncoding, AddCharset, AddDefaultCharset, DefaultType, ForceType, DirectoryIndex, RemoveType, AddHandler¹, SetHandler¹, RemoveHandler¹, Action¹

**Caching**: ExpiresActive, ExpiresByType, ExpiresDefault

**Environment**: SetEnv, SetEnvIf, SetEnvIfNoCase, BrowserMatch

**Containers**: Files, FilesMatch, Limit, LimitExcept

**Options**: Options

**Brute Force**: LSBruteForceProtection, LSBruteForceAllowedAttempts, LSBruteForceWindow, LSBruteForceAction, LSBruteForceThrottleDuration, LSBruteForceXForwardedFor, LSBruteForceWhitelist, LSBruteForceProtectPath

¹ *Parsed but no-op: OLS uses `scriptHandler` in vhost config instead of .htaccess handler directives. These directives are recognized and logged but do not change request handling.*

</details>

## Performance

### Benchmark Results (4C/8G, WordPress 6.9 + 19 plugins)

| Metric | Apache 2.4 | LiteHTTPD | Stock OLS | LSWS 6.3.5 |
|--------|-----------|-----------|-----------|------------|
| Static RPS (no htaccess) | 11,082 | 22,104 | 23,242 | 24,786 |
| Static RPS (200-line htaccess) | 10,618 | 21,960 | 18,883 | 20,306 |
| .htaccess overhead | -4.2% | **-0.7%** | -18.8% | -18.1% |
| Baseline memory | 969 MB | 676 MB | 663 MB | 819 MB |
| .htaccess compatibility | 100% | **90%+** | 44% | 100% |

Full results: [tests/e2e/vps-5engine/benchmark-results.md](tests/e2e/vps-5engine/benchmark-results.md)

### PHP Performance Tuning

LiteHTTPD uses lsphp (LSAPI protocol) for PHP processing. The default OLS configuration starts a single lsphp process and forks on demand, which causes slow cold starts under load. To match Apache PHP-FPM performance, configure lsphp worker pool size:

```
# In httpd_config.conf extprocessor section:
extProcessor lsphp {
    ...
    env                     PHP_LSAPI_CHILDREN=10
    env                     PHP_LSAPI_MAX_IDLE=300
    ...
}
```

| Config | PHP RPS (wp-login.php, c=5) | Behavior |
|--------|----------------------------|----------|
| Default (`CHILDREN=1`) | ~5 rps | Fork on demand, cold start ~1s per request |
| `CHILDREN=10` | ~15-20 rps | Pre-forked pool, no cold start |
| Apache PHP-FPM (pm=dynamic) | ~16 rps | Pre-forked pool (reference) |

> **Note**: Stock OLS and LSWS may show very high PHP RPS (20K-35K) in benchmarks due to built-in page caching (LSCache). This is the cache module returning stored responses, not actual PHP execution. LiteHTTPD does not include a page cache module — use LiteSpeed Cache WordPress plugin with OLS native cache module for equivalent behavior.

## Project

LiteHTTPD is a sub-project of [Web.Casa](https://web.casa), an AI-native open source server control panel.

Related project: [LLStack](https://llstack.com) -- a server management platform built on LiteHTTPD.

## License

This project is licensed under the GNU General Public License v3.0 -- see [LICENSE](LICENSE) for details.

OpenLiteSpeed is developed by LiteSpeed Technologies and is also GPLv3 licensed.
