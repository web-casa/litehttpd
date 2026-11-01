# Changelog

All notable changes to litehttpd will be documented in this file.

## [2.4.0] - 2026-11-01

Rebased onto OpenLiteSpeed 1.9.4 — carrying its security fixes — plus
the BotChallenge hardening from the external codex review.

### Changed — platform
- **Patched OLS base 1.9.1 → 1.9.4** (all four patches apply; verified
  against the v1.9.4 tree):
  - 1.9.4: **WebAdmin disabled by default** and admin_php rebuilt for
    **CVE-2026-109618 / CVE-2026-109619** ("php2shell", CVSS 9.2
    unauthenticated PHP RCE — our self-built admin_php uses PHP 7.4.33
    and cannot be source-patched; the default-off WebAdmin plus
    firewalling :7080 is the effective mitigation), brotli 1.2.1
    (CVE-2026-111497), 103 Early Hints, server push dropped.
  - 1.9.3: CGI execution and protocol security hardening.
  - 1.9.2: HTTP/2 + HTTP/3 header validation.
  - third-party pinned to 2f450154 (brotli v1.2.1 submodule; lsquic
    ships inside the OLS tree).
  - Known note: admin_php remains on EOL PHP 7.4 — keep WebAdmin off
    (the new default) or firewall it; moving it to a patched PHP 8.x
    is tracked as follow-up work.
- The module itself was already runtime-verified on 1.9.4 (CI E2E
  container) — only the release packaging pinned 1.9.1 until now.

### Fixed — BotChallenge (external codex review of 2.2.0..2.3.0)
- **Gate bypass via overlong URI** (HIGH): a 2047-byte URI of
  quote-only characters overflowed the 2048-byte JS-escape check, the
  challenge page failed to build, and the gate failed OPEN — the most
  hostile inputs were exactly the ones let through. Escape buffers
  sized for the worst case (8192) and the built-in page renders into
  the full buffer.
- **Solved-challenge answer left `?__lhc=<nonce>` in the URL** (HIGH,
  cache-poisoning adjacent): the cleanup rewrote only the path while
  OLS preserved the query — so a CDN could cache the credential-free
  200 as a PoW-free copy of the content. A solved nonce now gets a
  **302 to the clean URL** (other params kept; query dropped when
  __lhc was the only one); the browser lands on a canonical,
  cacheable address.
- **Remote-triggered signed overflow** (MED): multi-hundred-digit
  `__lhc` values overflowed a long during parsing (UB); oversized
  values are now rejected cleanly.
- Regression unit tests pin all three (suite 1161/1161); the E2E
  solver follows the real browser flow (302 + cookie jar).

## [2.3.0] - 2026-10-31

The self-hosted bot-defense release: LSWS Enterprise "managed challenge"
parity without any external service, plus CDN-correct identity binding.

### Added — BotChallenge (JS proof-of-work gate)
- **`BotChallenge On`** — clients without a valid credential cookie get an
  inline HTML+JS challenge; the browser solves an FNV-1a proof-of-work
  (daily salt, `BotChallengeDifficulty` bits, default 16, cap 24) and
  receives an HMAC-SHA256 cookie (`exp|hexmac`, secret + client IP class,
  constant-time verify, `BotChallengeTTL` seconds, floor 60). Stateless,
  no storage; cookie is `HttpOnly; SameSite=Lax; Path=/`; challenge
  responses are `Cache-Control: no-store`.
- **`BotChallengeTrustedProxy "cidr-list"`** — behind Cloudflare/Nginx the
  credential binds the RIGHTMOST `X-Forwarded-For` token when (and only
  when) the direct peer is a listed proxy; spoofable XFF from unlisted
  peers is ignored entirely. Fixes "every visitor shares the CDN's IP
  class" behind a proxy.
- **`BotChallengeIf "ap_expr"`** — per-request trigger condition (URI,
  headers, env). Pairs with the GeoIP pathway for country-scoped gating;
  unparsable expressions fail OPEN (gate off) with a WARN.
- **`BotChallengeExempt "regex"`** — UA allowlist (verified crawlers).
- **`BotChallengePage template.html`** — custom branded challenge page
  with `%{SALT}` `%{DIFF}` `%{URI}` substitution, realpath-confined to
  the .htaccess tree, O_NOFOLLOW; invalid/missing template falls back to
  the built-in page.
- **`tools/rotate-botsecret.sh`** — atomic HMAC-secret rotation (temp +
  rename, worker-user ownership, graceful OLS restart). RPM %post and
  tmpfiles.d provision `/run/litehttpd` for the OLS worker user.
- **Dynamic ESI includes** — `<esi:include>` of PHP fragments via loopback
  sub-requests with hold-and-join splicing (bounded, fail-open), plus
  handler-suffix fast-path exclusion so handler sources are never inlined.

### Fixed
- **Challenge-page script injection (hardening):** the request URI was
  embedded into the inline challenge script unescaped — a crafted request
  line (quotes / `</script>`) could inject script into the served page.
  All interpolations now go through a strict JS escaper (hex-escape
  outside `[A-Za-z0-9/_?.=&%-~]`), pinned by breakout unit tests.
- `RateLimit action=throttle` no longer sleeps inside the hook — a
  sleeping hook parks the worker and one client could occupy every
  worker (the trap behind upstream OLS issue #531). It now answers
  **429 + `Retry-After: <seconds-to-window-reset>`**.

### Testing
- Suite 1158/1158; +8 BotChallenge unit tests (crypto vectors, cookie
  matrix, XFF selection, JS-escape breakouts, template substitution).
- CI `E2E-6` (full PoW loop incl. trusted-proxy XFF class binding,
  conditional gate, custom template) and `E2E-7` — permanent regressions
  for upstream OLS issues #533 (gate bypass while pending) and #531
  (concurrent rate-limit batch release): 40-way concurrency must hit the
  configured ceiling EXACTLY.

## [2.2.0] - 2026-10-31

The Enterprise-parity feature release. Everything below is verified against
real OpenLiteSpeed 1.9.4 (x86_64 container CI: actual OLS + libmodsecurity
3.0.15 + nftables, running WordPress).

### Added — Enterprise feature bridges
- **Native RewriteRule/RewriteCond execution** (the headline): stock OLS now
  executes rewrites with a built-in PCRE2 engine — no patch required.
  Apache mod_rewrite subset: `R[=301..308] L END NC QSA F G`, `!` negation,
  `$0..$9` groups, `%{VAR}` expansion, `-f/-d/-s` / `<` `>` `=` conditions,
  `NC`/`OR` cond flags, `RewriteBase`, QSA. Patched OLS keeps its fast path.
- **ModSecurity bridge**: `SecRuleEngine Off` per directory — an LSWS
  Enterprise feature — via an early URI hook that wins the env race against
  the WAF (verified: SQLi probe 403 outside, 200 inside a bridged dir).
- **ESI**, opt-in per directory with `EsiEnable`: `<esi:include>` (static
  fragments, realpath-contained to the docroot), `<esi:remove>`,
  `<esi:comment>`, `<esi:vars>` substitution; chunk-boundary reassembly so
  split tags never leak; PHP-handler output filtered end to end.
- **Firewall full chain**: brute-force thresholds escalate to kernel-level
  nftables drops via the `litehttpd-firewalld` daemon (input + forward
  hooks, kernel TTL timeouts, persistence, once-per-window notification).
  Verified transcript: 200x3 -> 403 -> kernel drop -> TTL auto-expiry.
- **LSCache bridge**: `CacheEnable public[,esi=on]` / `CacheDisable` /
  `CacheMaxExpire` / `CacheDefaultExpire` drive the OLS cache module from
  `.htaccess`; `,esi=on` marks fragment-aware pages.
- `RateLimit N/M [burst=B] [action=deny|throttle|log]` per-IP limiting.

### Fixed — found by real-OLS container e2e (each shipped broken before)
- `ls_shmhash_get` called with 4 args against OLS's 5-arg API — wrote a
  flag through a garbage register pointer; **every brute-force check
  segfaulted the worker**. Signatures now match `ls_shm.h` exactly.
- Module .so deployed without `htaccess_stream.c` (undefined symbol — load
  failure) — CMake target fixed.
- Brute-force POST counting had no caller outside the auth-fail branch:
  form-logins (WordPress) were never blocked.
- nftables backend never created its table/set (`add element` always
  failed); TTL was hardcoded. Now idempotent layout + kernel timeouts.
- ESI filter returned `len1` without `stream_write_next` — consumed input
  is not auto-forwarded in the OLS stream contract; the body would have
  been dropped once ESI engaged. Backpressure semantics corrected too.
- ESI include resolved `src` against a hardcoded `/` docroot (arbitrary
  file read, caught before release by the codex probe + self-review);
  now session-docroot-bounded with realpath containment.
- RateLimit and brute-force shared one SHM record namespace — rate-limited
  normal clients fed login-failure counters. Namespaced `b:`/`r:`.
- Firewall client `connect()` was unbounded on AF_UNIX (socket timeouts do
  not cover it); now non-blocking connect + poll deadline.
- Escalation fired on every over-threshold request; now once per window.

### Testing
- Suite 1131/1131; directive/printer/generator consistency 0 gaps.
- New GitHub Actions job `E2E (OLS container)`: five live checks (htaccess
  header, native rewrite redirect, ESI include + comment strip, firewall
  full chain with kernel drop, modsec bridge) on every push/PR.
- Hook-level regression tests pin the stream-filter contract; stub-daemon
  tests pin the firewall wire protocol; split-tag ESI tests.

## [2.1.5] - 2026-07-05

### Changed
- Release metadata and final release gates for v2.1.5.

## [2.1.4] - 2026-06-11

Security hardening release. Audited with a multi-agent review plus external
(Codex) cross-review; all identified fail-open paths are closed. No functional
directives were removed; a few behaviours are intentionally stricter (see
"Changed" / "Security").

### Security
- **Auth fail-open fixed (critical):** `Require user` and `Require group` were
  silently dropped by the parser, leaving a directory protected by
  `AuthType Basic` served to everyone with no authentication. `Require user`
  is now fully implemented (username-list enforcement); `Require group` and any
  other unrecognised `Require` form now **fail closed** (deny) instead of being
  dropped. This also applies inside `<RequireAny>`/`<RequireAll>` (previously
  dropped by the container allow-list).
- **`<RequireAll>` AND semantics for usernames:** multiple `Require user` lists
  inside `<RequireAll>` are now AND-combined (must match all), not OR-combined.
- **Nested container logic fix:** a `<RequireAll>` nested in `<RequireAny>` no
  longer leaks its children into the outer `RequireAny` (which silently turned
  AND into OR).
- **AuthUserFile confinement:** `AuthUserFile` is now restricted to the
  document-root subtree and opened with `O_NOFOLLOW` + post-open `/proc/self/fd`
  re-validation (closes a symlink/TOCTOU path-escape that allowed using the
  Basic-Auth endpoint as a password-test oracle against arbitrary files such as
  `/etc/shadow`). `check_auth_credentials()` shares the same confined opener.
- **Brute-force X-Forwarded-For spoofing fixed:** `BruteForceXForwardedFor` now
  requires the new **`BruteForceTrustedProxy <cidr-list>`** directive; XFF is
  only honoured when the direct peer is a trusted proxy, the rightmost
  (proxy-added) token is used, validated as an IP, and length-capped. Without a
  trusted-proxy list, XFF is ignored and the real peer IP is used.
- **Shared-memory hardening:** brute-force SHM uses LiteHTTPD-private segment
  names (no longer collides with CyberPanel's `BFProt`/`IPQuota`) and a
  magic/version guard, preventing cross-process record-layout corruption.
- **Expression-engine DoS guards:** `tolower()`/`toupper()` nesting is now
  depth-limited (stack-exhaustion), and regex matching caps untrusted subject
  length (ReDoS).
- **Memory-safety / overflow:** length-bounded URI scanning (no OOB reads on a
  non-NUL-terminated request URI), `Expires` duration integer-overflow guards,
  and a `time_t` overflow cap on the `Expires` date.

### Added
- `Require user <u1 u2 ...>` username-list authorization.
- `BruteForceTrustedProxy <cidr-list>` directive.
- **Binary tarball distribution** (distro-independent, glibc 2.28+), alongside
  the RPMs, published to the GitHub Release and `rpms.litehttpd.com`:
  - `openlitespeed-litehttpd-<ver>-x86_64-linux.tgz` — complete patched OLS +
    module + `install.sh` (fresh install, like the official OLS tarball).
  - `litehttpd-ols-patch-<ver>-x86_64-linux.tgz` — overlay for an existing
    **official OLS of the same version**: backs up and replaces the binary +
    adds the module (full features; install.sh verifies the version).
  - `litehttpd-module-<ver>-x86_64-linux.tgz` — module-only overlay for an
    existing OLS of **any** version (no binary change).

### Changed
- `AuthUserFile` outside the document root is now rejected (was: any absolute
  path). Place `.htpasswd` within the site's document root.
- `BruteForceXForwardedFor On` now has no effect unless
  `BruteForceTrustedProxy` is also configured (fail-safe default).
- Build now applies exploit-mitigation flags (`-fstack-protector-strong`,
  `-fstack-clash-protection`, `_FORTIFY_SOURCE=2`, full RELRO, `-z noexecstack`,
  `-Werror=format-security`); the RPM spec passes distro `%optflags`.

### Notes
- Known fail-CLOSED limitation: a mixed `<RequireAny>` combining a non-auth
  branch (e.g. `Require ip`) with `Require user` is evaluated more strictly than
  Apache — a user from the allowed IP who is not in the user list is still
  challenged/denied. This over-denies (safe) rather than risking a fail-open.

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
