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
- **litehttpd-confconv**: Apache `httpd.conf` to OLS config converter (65+ directives, panel detection, Macro support, SSL extended)
- **readApacheConf**: Embedded Apache config parser in OLS binary (like CyberPanel/LSWS)

## Quick Start

### LiteHTTPD-Full (RPM, EL 8/9/10)

One RPM installs everything: patched OLS + litehttpd module + auto-configuration.

```bash
# Install patched OLS (includes bundled module, auto-configured)
rpm -ivh openlitespeed-litehttpd-1.8.5-2.litehttpd.el9.x86_64.rpm

# Install lsphp83 (from LiteSpeed repo)
dnf install lsphp83 lsphp83-common lsphp83-mysqlnd lsphp83-gd \
    lsphp83-mbstring lsphp83-xml lsphp83-pdo lsphp83-opcache

# Restart OLS
/usr/local/lsws/bin/lswsctrl restart
```

The RPM `%post` automatically:
- Enables the litehttpd module in `httpd_config.conf`
- Configures lsphp83 path and `PHP_LSAPI_CHILDREN=10`
- Enables vhost rewrite (for WordPress permalinks)
- Adds `index.php` to indexFiles

### LiteHTTPD-Thin (stock OLS, any platform)

For stock (unpatched) OLS. Download the `.so` matching your OS:

```bash
# Download module for your platform (el8/el9/el10/ubuntu22, x86_64/aarch64)
curl -LO https://github.com/web-casa/litehttpd/releases/latest/download/litehttpd_htaccess-el9-x86_64.so
cp litehttpd_htaccess-el9-x86_64.so /usr/local/lsws/modules/litehttpd_htaccess.so

# Enable module
cat >> /usr/local/lsws/conf/httpd_config.conf <<'EOF'

module litehttpd_htaccess {
    ls_enabled              1
}
EOF

# Enable rewrite in vhost (required for WordPress permalinks)
VHCONF=/usr/local/lsws/conf/vhosts/Example/vhconf.conf
sed -i '/^rewrite {/,/^}/s/enable 0/enable 1/' $VHCONF
sed -i 's/indexFiles index.html/indexFiles index.php, index.html/' $VHCONF

# Restart OLS
/usr/local/lsws/bin/lswsctrl restart
```

> **Note**: Thin mode supports 70+ directives. RewriteRule and php_value are parsed but not executed (requires Full mode patches).

## Apache Config Conversion

LiteHTTPD provides two ways to convert Apache `httpd.conf` to OLS format, using the same parser (65+ directives, panel detection, Macro support, SSL extended):

### readApacheConf (recommended for production)

Add one line to OLS config -- Apache config is parsed **in-process** at startup (like CyberPanel/LSWS):

```
# In /usr/local/lsws/conf/httpd_config.conf:
readApacheConf /etc/httpd/conf/httpd.conf portmap=80:8088,443:8443
```

OLS starts, parses the Apache config, generates native OLS vhost/listener configs, and loads them automatically. No external tools, no fork/exec.

### litehttpd-confconv CLI (for migration, debugging, CI)

Standalone tool for manual conversion, change detection, and hot-reload:

```bash
# One-time conversion
litehttpd-confconv --input /etc/httpd/conf/httpd.conf \
                   --output /usr/local/lsws/conf/apacheconf/ \
                   portmap=80:8088,443:8443

# Check if Apache config changed (for CI/scripts)
litehttpd-confconv --check /etc/httpd/conf/httpd.conf --state /var/run/confconv.state
# Exit: 0=changed, 1=unchanged, 2=error

# Watch mode (auto-reconvert on changes)
litehttpd-confconv --watch /etc/httpd/conf/httpd.conf --interval 60 \
                   --output /usr/local/lsws/conf/apacheconf/
```

Both use the same parser. `readApacheConf` is for "set and forget" production use; the CLI is for migration previews, debugging, CI pipelines, and llstack integration.

## OLS Patches

Four patches extend OLS for full functionality:

| Patch | Description |
|-------|-------------|
| `0001-lsiapi-phpconfig.patch` | PHPConfig LSIAPI extensions (php_value/php_flag to lsphp) |
| `0002-lsiapi-rewrite.patch` | RewriteRule engine LSIAPI extensions (parse/exec/free) |
| `0003-readapacheconf.patch` | Embedded Apache config parser + readApacheConf directive |
| `0004-autoindex-403.patch` | Options -Indexes returns 403 (match Apache behavior) |

Apply to OLS 1.8.5 source before building:

```bash
cd openlitespeed-1.8.5/
# Copy confconv parser/writer into OLS source tree (needed by patch 0003)
cp /path/to/apacheconf_parser.{c,h} src/main/
cp /path/to/ols_config_writer.{c,h} src/main/
# Apply patches
patch -p1 < 0001-lsiapi-phpconfig.patch
patch -p1 < 0002-lsiapi-rewrite.patch
patch -p1 < 0003-readapacheconf.patch
patch -p1 < 0004-autoindex-403.patch
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

### Build

EL8/EL9/EL10 RPMs with fully static linking (BoringSSL, pcre, all deps from source).

### Build without tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build -j$(nproc)
```

## Testing

The project includes 1036 tests across four test suites:

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

### Benchmark Results (v2.0.0, Linode 4C/8G EL9, WordPress, PHP 8.3, MariaDB 10.11)

#### Functional Compatibility

| Test | Apache | OLS-Thin | OLS-Full | Stock OLS |
|------|--------|----------|----------|-----------|
| WordPress Home | ✅ 200 | ✅ 200 | ✅ 200 | ✅ 200 |
| Pretty Permalink | ✅ 200 | ✅ 200 | ✅ 200 | ✅ 200 |
| Security Headers (nosniff, X-Frame) | ✅ | ✅ | ✅ | ❌ |
| wp-config.php blocked | ✅ 403 | ✅ 403 | ✅ 403 | ❌ 200 |
| xmlrpc.php blocked | ✅ 403 | ✅ 403 | ✅ 403 | ❌ 405 |
| .htaccess blocked | ✅ 403 | ✅ 403 | ✅ 403 | ✅ 403 |
| **Pass rate** | **10/10** | **10/10** | **10/10** | **6/10** |

#### Performance

| Metric | Apache | OLS-Thin | OLS-Full | Stock OLS |
|--------|--------|----------|----------|-----------|
| Static RPS | 23,256 | 49,242 | **57,037** | 69,920 |
| PHP RPS (wp-login) | **284** | 111 | 124 | 111 |
| Permalink RPS | **74** | 4.2 | 2.8 | 2.1 |
| Server RSS | 97 MB | **44 MB** | **47 MB** | 42 MB |
| Total Memory | 793 MB | **670 MB** | 773 MB | 717 MB |
| Process count | 8 | 9 | 11 | 19 |

- **OLS-Thin**: Stock OLS + litehttpd_htaccess.so module (70+ directives)
- **OLS-Full**: Patched OLS (4 patches, source-compiled) + module (80 directives)
- **Stock OLS**: No .htaccess module, native vhost rewrite only

**Key findings**:
- OLS-Full static performance is **2.5x Apache** with **half the memory** (47 MB vs 97 MB)
- OLS-Thin and OLS-Full achieve **100% Apache .htaccess compatibility** (10/10)
- Apache PHP-FPM (`pm=static`, 10 children) outperforms OLS lsphp default fork mode -- configure `PHP_LSAPI_CHILDREN=10` to improve
- Permalink (rewrite + PHP routing) is slow across all OLS engines (2-4 rps) -- this is an OLS architectural limitation where native rewrite + PHP routing has higher overhead than Apache mod_rewrite

Previous benchmark (5-engine, 19 WP plugins): [tests/e2e/vps-5engine/benchmark-results.md](tests/e2e/vps-5engine/benchmark-results.md)

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

## Troubleshooting

### WordPress permalinks return 404

OLS defaults to `rewrite { enable 0 }` in vhost config. The `.htaccess` RewriteRule is parsed by litehttpd but OLS's rewrite engine won't execute it unless vhost-level rewrite is enabled.

**Fix**: Edit your vhost config (e.g., `/usr/local/lsws/conf/vhosts/Example/vhconf.conf`):

```
rewrite {
  enable                  1    # <-- change from 0 to 1
  logLevel                0
}
```

Then restart OLS: `/usr/local/lsws/bin/lswsctrl restart`

### PHP files return 403 "access denied"

OLS needs a `scriptHandler` mapping PHP to lsphp. Check that your `httpd_config.conf` has:

```
scriptHandler {
    add lsapi:lsphp php    # name must match extProcessor name
}
```

And the `extProcessor lsphp` path points to the correct lsphp binary (e.g., `$SERVER_ROOT/lsphp83/bin/lsphp`).

### Directory index returns 404 (e.g., /wordpress/)

OLS default `indexFiles` only includes `index.html`. Add `index.php`:

```
index {
    indexFiles index.php, index.html
}
```

## Project

LiteHTTPD is a sub-project of [Web.Casa](https://web.casa), an AI-native open source server control panel.

Related project: [LLStack](https://llstack.com) -- a server management platform built on LiteHTTPD.

## License

This project is licensed under the GNU General Public License v3.0 -- see [LICENSE](LICENSE) for details.

OpenLiteSpeed is developed by LiteSpeed Technologies and is also GPLv3 licensed.
