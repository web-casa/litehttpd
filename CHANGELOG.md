# Changelog

All notable changes to litehttpd will be documented in this file.

## [2.0.3] - 2026-04-09

### Fixed
- Pin litehttpd + third-party git clones to version tags (supply-chain safety)
- Module build failure is now FATAL (was silently skipped)
- Use %{_builddir} instead of /tmp for inter-phase artifacts (no race condition)
- Binary-safe buildroot cleanup (grep -rlI skips all binary formats)
- %post uses idempotent grep guards for both install AND upgrade
- Rewrite enable sed narrowed to rewrite{} block only

## [2.0.2] - 2026-04-09

### Added
- litehttpd_htaccess.so module bundled inside OLS RPM
- RPM %post auto-enables module in httpd_config.conf
- One RPM install = patched OLS + module + auto-configured

## [2.0.1] - 2026-04-08

### Fixed
- RPM now contains all 4 patches (was only patch 0003)
  Root cause: rpmbuild __spec_install_post replaced patched binary
  Fix: disable post-processing + save/restore patched binary
- All RPMs built on AlmaLinux 8 (glibc 2.28) for EL8/9/10 compatibility
- RPM %post auto-configures rewrite enable, indexFiles, PHP workers

## [2.0.0] - 2026-04-08

### Added
- Phase 7a: WordPress uploads PHP protection, ErrorDocument 401 security, adaptive cache
- Phase 7b: If/ElseIf/Else conditional blocks with Expression engine
- Phase 7b: RewriteOptions inherit/IgnoreInherit, RewriteMap txt/rnd/int
- Phase 8: IPv6 CIDR access control, Require env, Require ip prefix matching
- Phase 8: HTACCESS_VHROOT three-level .htaccess merging
- Phase 9: RemoveType, RemoveHandler, Action directives
- Phase 10: litehttpd-confconv Apache-to-OLS config converter (40 directives)
- Phase 11: litehttpd-confconv extended (60+ directives, OLS patch, hot reload)
- Phase 12: readApacheConf embedded in-process Apache config parser (like LSWS/CyberPanel)
- Phase 13a: Complete ap_expr AST engine (&&, ||, !, parens, -ipmatch, functions)
- confconv: Panel detection (DirectAdmin, InterWorx, CyberCP)
- confconv: SSL extended directives (SSLVerifyClient, SSLVerifyDepth, SSLSessionTickets, SSLOCSPDefaultResponder, SSLCACertificateFile/Path, SSLCARevocationFile)
- confconv: Macro support (<Macro>/<Use> with $param/@param substitution)
- confconv: PHP path mapping (version extraction, normalized lsphpXY format)
- confconv: Context inheritance (docroot Directory merging, allowOverride inheritance)
- OLS Patch 0003: readApacheConf startup integration
- OLS Patch 0004: Options -Indexes returns 403 (match Apache autoindex behavior)
- Full static build matching official LiteSpeed (BoringSSL, pcre-8.45, all libs from source)
- EL8/EL9/EL10 RPM builds verified
- Property-based test generators for all 80 directive types
- 1036 total tests (786 unit + 124 property + 52 compat + 55 confconv + 19 new)

### Fixed
- PHPConfig type mapping (1=VALUE, 2=FLAG, 3=ADMIN_VALUE, 4=ADMIN_FLAG)
- .ht* file protection (403 for .htaccess/.htpasswd)
- Redirect priority (executes before RewriteRule, matching Apache mod_alias)
- CRLF injection protection on Header/Redirect values
- If nesting inside Files/FilesMatch/IfModule containers
- IPv4-mapped IPv6 ACL bypass prevention
- Expression engine TLS buffer aliasing fix
- Multiple Codex review security fixes (7 rounds)
- ErrorDocument use-after-free in URI_MAP handler
- Macro parameter boundary (delimiters for <Directory $path>)
- Block-bearing macros (fmemopen in-memory stream parsing)
- Context inheritance security (parent Require denied propagation)
- Recursive macro protection (depth limit 16)
- SSLCARevocationFile emission (CRLFile output)
- SSLVerifyDepth validation (strtol + range check)
- Panel detection config path checks

### Security
- 4 rounds of manual security audit + 7 rounds of Codex automated review
- Codex full codebase review: 7 HIGH + 11 MEDIUM + 4 LOW findings fixed
- All CRITICAL and HIGH findings resolved
- Thread-local regex and negative stat caching for performance hardening
- Header injection protection (CRLF filtering)
- ExecCGI/cgi-script execution blocked via AllowOverride
- Environment variable blacklist (SERVER_SOFTWARE, PATH, etc.)
- .htaccess file size limit enforcement
- ErrorDocument path traversal blocked (reject ".." in local paths)
- SHM brute-force atomic increment (prevent race condition)
- Request cache stale data prevention (clear at on_uri_map entry)
- Include symlink traversal blocked (realpath + server_root check)
- RewriteMap unsafe types blocked (allowlist: txt/rnd/int/dbm only)
- php_admin_value/flag blocked from .htaccess (prevent disable_functions override)
- CRLF injection guard on all header setters
- DirectoryIndex path traversal blocked
- Brute force throttle overflow protection

## [1.0.0] - 2026-03-15

### Added
- Initial release with 71 .htaccess directives
- Header set/unset/append/merge/add with always variants and edit/edit*
- RequestHeader set/unset
- php_value, php_flag, php_admin_value, php_admin_flag (dual-mode: native API + env-var fallback)
- Order/Allow/Deny access control
- Redirect and RedirectMatch with status codes
- ErrorDocument for custom error pages
- ExpiresActive, ExpiresByType, ExpiresDefault
- SetEnv, SetEnvIf, SetEnvIfNoCase, BrowserMatch
- FilesMatch, Files, IfModule containers
- Options directive (Indexes, FollowSymLinks, MultiViews, ExecCGI)
- Require all granted/denied, Require ip/not ip, RequireAny/RequireAll
- Require valid-user with AuthType Basic, AuthName, AuthUserFile
- Limit/LimitExcept containers
- AddType, AddHandler, SetHandler, ForceType, AddEncoding, AddCharset
- DirectoryIndex
- RewriteEngine, RewriteBase, RewriteCond, RewriteRule
- AddDefaultCharset, DefaultType, Satisfy
- AllowOverride category filtering (5 categories)
- LiteSpeed brute-force protection (8 directives)
- OLS native API integration via dlsym (ls_hash, ls_shmhash)
- Dual-mode cache (OLS native + djb2 fallback)
- Dual-mode SHM (OLS ls_shmhash + in-memory fallback)
- 838 tests (662 unit + 124 property + 52 compat)
- RPM packaging for EL 9/10 (x86_64 + aarch64)
- CI pipeline with ASan/UBSan, consistency checks, E2E tests
- OLS patches: PHPConfig (0001) + Rewrite (0002)
