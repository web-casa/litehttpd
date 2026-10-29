# Handoff Notes

This subtree is the apps-matrix test harness. Current release-readiness work
also includes module compatibility fixes that were exposed by real CMS runs.

## What Is Already Done

- scenario inventory for all 12 agreed app/plugin cases
- declarative `cases.yaml` per scenario
- scenario-local `install.sh` / `verify.sh` contracts
- shared result schema
- shared case contract documentation
- top-level runner that can enumerate and plan scenarios
- shared three-engine request executor
- assertion engine for the primitives in `CASES_SCHEMA.md`
- `summary.csv`, `known_diff.csv`, `failures.csv`, per-case JSON, and per-engine artifacts
- deterministic stub fixture provisioning for all scenarios
- overlay-mode probe/asset deployment for externally installed real apps
- scenario install orchestration through `run_matrix.sh --install`
- case and tier filtering through `--case` / `--tier`
- per-result JSON structure validation
- CI-safe fixture smoke runner
- real fixture installer framework with pinned versions in `config/versions.env`
- full matrix runner for Apache / OLS module / OLS native
- heavy app install flows for WordPress, Laravel, Drupal, Nextcloud, Joomla,
  and MediaWiki behind `MATRIX_REAL_RUN_APP_INSTALL=1`
- probe hardening for real/overlay fixtures:
  unmanaged `_probe` conflict detection, `enabled|local-only|disabled` policy,
  and probe manifests
- fixture-builder image entrypoint in `Dockerfile.fixture`
- CI caches for downloaded app archives and Composer packages
- manual heavy workflow coverage across all 12 apps-matrix scenarios via Actions
  matrix
- app-specific deep config manifests and validation through
  `.apps-matrix/app_config.json`
- installer cache hit/miss reporting in `real-install-cache.csv`
- installer step timing in `real-install-steps.csv`
- per-engine request timing in `performance.csv`
- scenario timing and optional budgets in `full-matrix-timing.csv`
- manual heavy real-install workflow with `MATRIX_REAL_RUN_APP_INSTALL=1`
- tuned budget defaults in `config/budgets.env` and suggestions through
  `lib/tune_budgets.py`
- install/verify split support with docroot artifacts and `db-dumps/*.sql.gz`
  handoff
- deeper app-native config checks for installed WordPress, Laravel, Drupal,
  Nextcloud, Joomla, and MediaWiki
- WordPress plugin option snapshot at
  `.apps-matrix/wordpress_plugin_config.json`
- WordPress plugin scenarios require a plugin health probe when health probes
  are enabled
- password-free Laravel config snapshot at
  `.apps-matrix/laravel_config.json`
- disposable DigitalOcean VPS reality runner in
  `scripts/apps-matrix-do-vps-test.sh`
- app-native post-install health probes and `real-install-health.csv`
- redacted real-install command headers for DB/admin password arguments
- `MATRIX_REAL_STRICT_PLUGIN_INSTALL=auto` for plugin scenarios
- `MATRIX_SCENARIOS=heavy` preset for the DB-backed heavy set
- summary/full artifact manifests through `lib/collect_artifact_manifest.py`
- budget ratchet support through `lib/tune_budgets.py --update-env`
- semantic JSON snapshots for Laravel, Drupal, Nextcloud, Joomla, and MediaWiki
- `MATRIX_SCENARIOS=wordpress|cms|framework` app-family subsets
- fixture cache prewarm entrypoint through `prewarm_fixture_cache.sh`
- heavy workflow `family` and `prewarm_cache` controls
- prebuilt runtime controls through `MATRIX_COMPOSE_BUILD=0`,
  `MATRIX_APACHE_IMAGE`, and `MATRIX_OLS_MODULE_IMAGE`
- heavy workflow one-shot GHCR runtime image builds with BuildKit cache, so
  scenario jobs can pull the same Apache and patched `ols_module` images instead
  of compiling them independently; repeated runs can skip existing one-shot tags
  through `runtime_images_reuse_existing=1`
- reusable runtime-image workflow in
  `.github/workflows/apps-matrix-runtime-images.yml` for per-SHA Apache and
  patched `ols_module` GHCR images
- runtime-image workflow builds Apache and patched OLS images in parallel and
  skips rebuilding already-published GHCR tags by default through
  `reuse_existing=1`
- manual heavy and DO release-gate jobs consume shared runtime images with
  `MATRIX_COMPOSE_BUILD=0` and expose `runtime_images_reuse_existing` for
  deliberate manual tag refreshes
- DO release-gate workflow in `.github/workflows/apps-matrix-do-release-gate.yml`
  runs disposable VPS validation with prebuilt runtime images instead of remote
  Docker builds, and exposes the same runtime-image reuse switch for direct
  dispatches
- DO VPS runner registry login and pre-pull support for private prebuilt
  Apache and `ols_module` images
- DO VPS runner can skip remote dependency installation when using a prepared
  snapshot via `APPS_MATRIX_DO_SKIP_DEP_INSTALL=1`
- DO snapshot preparation script in
  `scripts/apps-matrix-do-prepare-base-image.sh`
- budget ratchet wrapper in `scripts/apps-matrix-ratchet-budgets.sh`, backed by
  sample-count checks in `lib/tune_budgets.py`
- compose readiness uses the harness PHP probe by default through
  `MATRIX_READY_PATH=/_probe/server.php`; Drupal defaults to
  `MATRIX_READY_PATH_DRUPAL=/core/misc/drupal.js` because stock Drupal
  `.htaccess` denies non-whitelisted PHP files and the homepage depends on full
  Drupal PHP bootstrap
- Drupal real-app cases use `/user/login` for dynamic front-controller health.
  The synthetic `_probe/router.php` JSON route is reserved for stub fixtures
  because real Drupal consumes clean URLs through its own router.
- module parsing/execution now covers Drupal-style `FallbackResource /index.php`
  and Apache `Header onsuccess ...` syntax observed in stock CMS `.htaccess`
- patched OLS runtime Docker builds default to `OLS_RPM_BUILD_MODE=build`, which
  stops after `%build` and copies the patched OLS binary/module from
  `litehttpd-artifacts`; use `OLS_RPM_BUILD_MODE=package` only for full RPM
  `%install` validation
- ModSecurity source builds now target only the `src` static library needed by
  OLS packaging/runtime images, instead of compiling unused rules-check,
  benchmark, unit, and regression binaries

## Release-Gate Operating Plan

1. Operate and ratchet real-install coverage
   - run the heavy workflow or DO release-gate workflow across
     `MATRIX_SCENARIOS=heavy`
   - use `scripts/apps-matrix-ratchet-budgets.sh` after enough p95 samples exist
   - enable budget enforcement when timing variance is understood

2. Expand app-native strictness when stable upstream output allows it
   - add more app-specific semantic checks beyond current health probes
   - tighten non-WordPress checks only when installer output is stable across
     supported versions

3. Keep runtime realistic without rebuilding the world
   - publish or let the reusable runtime-image workflow prebuild Apache and
     patched OLS runtime images
   - rely on `reuse_existing=1` for reruns of the same SHA/tag, and set it to
     `0` only when a tag must be deliberately refreshed
   - run heavy workflow or DO with `MATRIX_COMPOSE_BUILD=0` and
     `MATRIX_APACHE_IMAGE=<image>` plus `MATRIX_OLS_MODULE_IMAGE=<image>`
   - if local patched-OLS image builds are unavoidable, keep the default
     `OLS_RPM_BUILD_MODE=build`; reserve `OLS_RPM_BUILD_MODE=package` for
     release packaging checks that need WebAdmin `admin_php`
   - use prepared DO snapshots with `APPS_MATRIX_DO_SKIP_DEP_INSTALL=1` for
     release-gate reality checks that do not need cold-start package install
   - keep DO VPS tests focused on release-gate reality checks

## Non-Goals For This Subtree

- no hidden pass conditions for known diffs
- no weakening of app behavior checks to hide real Apache/OLS differences
