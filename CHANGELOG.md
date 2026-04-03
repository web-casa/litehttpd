# Changelog

All notable changes to litehttpd will be documented in this file.

## [2.0.0] - 2026-04-01

### Added
- Phase 7a: WordPress uploads PHP protection, ErrorDocument 401 security, adaptive cache
- Phase 7b: If/ElseIf/Else conditional blocks with Expression engine
- Phase 7b: RewriteOptions inherit/IgnoreInherit, RewriteMap txt/rnd/int
- Phase 8: IPv6 CIDR access control, Require env, Require ip prefix matching
- Phase 8: HTACCESS_VHROOT three-level .htaccess merging
- Phase 9: RemoveType, RemoveHandler, Action directives
- Phase 10: litehttpd-confconv Apache-to-OLS config converter (40 directives)
- Phase 11: litehttpd-confconv extended (60+ directives, OLS patch, hot reload)
- Phase 13a: Complete ap_expr AST engine (&&, ||, !, parens, -ipmatch, functions)
- OLS Patch 0003: readApacheConf startup integration
- Property-based test generators for all 80 directive types
- 1017 total tests (786 unit + 124 property + 52 compat + 55 confconv)

### Fixed
- PHPConfig type mapping (1=VALUE, 2=FLAG, 3=ADMIN_VALUE, 4=ADMIN_FLAG)
- .ht* file protection (403 for .htaccess/.htpasswd)
- Redirect priority (executes before RewriteRule, matching Apache mod_alias)
- CRLF injection protection on Header/Redirect values
- If nesting inside Files/FilesMatch/IfModule containers
- IPv4-mapped IPv6 ACL bypass prevention
- Expression engine TLS buffer aliasing fix
- Multiple Codex review security fixes (7 rounds)

### Security
- 4 rounds of manual security audit + 7 rounds of Codex automated review
- All CRITICAL and HIGH findings resolved
- Thread-local regex and negative stat caching for performance hardening
- Header injection protection (CRLF filtering)
- ExecCGI/cgi-script execution blocked via AllowOverride
- Environment variable blacklist (SERVER_SOFTWARE, PATH, etc.)
- .htaccess file size limit enforcement

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
