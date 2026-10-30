# Patch Lifecycle Decision Record

Status: v1 (P2 deliverable). Each patch gets a verdict and a maintenance plan.

## 0001-lsiapi-phpconfig.patch
- **What it does**: Adds PHP config API (set_php_config_value/flag/get) and
  set_req_header to the lsi_api_t struct; exports litehttpd_lsiapi_extensions_v1 marker.
- **Issue**: set_req_header is NULL even in this patch (shim:587 repurposes it
  for set_dir_option which never fires). The marker doesn't validate struct size.
- **Verdict**: FIX + UPSTREAM. Update to export `litehttpd_abi_info` with
  version/size/bitmap (spec: abi-handshake.md). Fix set_req_header to actually
  set request headers. This is the most upstreamable patch (pure API extension,
  no behavioral change to stock OLS).
- **OLS version**: rebase from 1.9.1 → target version (P2 decision below)

## 0002-lsiapi-rewrite.patch
- **What it does**: Adds RewriteRule parse/exec/free API for OLS-internal rewrite.
- **Issues**: Creates stack-allocated `HttpContext tmpCtx` passed to parseRules
  (lifetime concern); uses `RewriteEngine::getInstance()` singleton across workers.
- **Verdict**: REVIEW. Evaluate moving rewrite execution into the module (with
  our own PCRE2 engine from P1) to eliminate this patch entirely. If keeping,
  fix the tmpCtx lifetime and document the singleton constraint.
- **Dependency**: P10 RE2 evaluation determines in-module rewrite feasibility.

## 0003-readapacheconf.patch
- **What it does**: Embeds ~2.7K lines (apacheconf_parser.c + ols_config_writer.c)
  into OLS main to read Apache-format config files at startup.
- **Issues**: Large patch surface; adds config-write path inside the server;
  hardest to rebase; upstream will not accept.
- **Verdict**: RETREAT. Move Apache config conversion to a standalone tool
  (litehttpd-confconv already exists in CMakeLists) run at install time,
  generating OLS-format conf. Patch shrinks to zero or a 5-line hook.

## 0004-autoindex-403.patch
- **What it does**: Patches OLS core to fopen .htaccess and scan Options tokens
  for engine-level `Options -Indexes`. Bypasses module API entirely.
- **Issues**: No IfModule support, no directory inheritance, no size limit,
  no symlink check. Duplicate parsing logic.
- **Verdict**: DEPRECATE. Once 0001 exports a proper set_dir_option (or we use
  the existing env-var fallback), this patch is unnecessary. Remove in P3.

## OLS version strategy
- Current test infrastructure pins 1.9.1 (Dockerfile, check_patches.sh).
- Decision: **stay on 1.9.1** through P3-P5 for stability. Evaluate 1.9.4
  rebase at P11 alongside upstream PR preparation. The ABI handshake makes
  version transitions safer (struct size check catches mismatches).

## Upstream PR candidates (priority order)
1. ABI info export (from 0001, trivial, pure additive)
2. PHP config API (from 0001, additive, useful to any module author)
3. set_req_header fix (from 0001, bug fix in their own patch infrastructure)
