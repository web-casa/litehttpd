# Apps Matrix E2E Harness

This directory contains a test-only harness for running real PHP application
scenarios with `.htaccess`-heavy behavior across three engines:

- `apache`
- `ols_module`
- `ols_native`

The goal is behavioral comparison, not just "app boots".

## Status Model

Each case must end in one of:

- `PASS_EQUIV`: all required outputs match Apache closely enough
- `PASS_KNOWN_DIFF`: app still works, but a documented Apache/OLS difference exists
- `FAIL_REGRESSION`: OLS behavior diverges unexpectedly from Apache
- `FAIL_APP_BROKEN`: the app itself is not healthy enough to compare

`PASS_KNOWN_DIFF` is allowed, but must be emitted into a separate red-table
report so drift is visible.

## Required Assertions

Cases should compare some subset of:

- exact status code
- exact `Location`
- response headers
- response-body markers
- probe output from `_probe/*.php`
- TTL derived from `Cache-Control` / `Expires`

## Execution Model

`run_matrix.sh` now executes each scenario through the shared Python runner in
`lib/run_cases.py`. The runner:

- fetches each case against `apache`, `ols_module`, and `ols_native`
- stores headers/body/curl artifacts per engine
- evaluates the assertion primitives from `CASES_SCHEMA.md`
- writes `summary.csv`, `known_diff.csv`, `failures.csv`, and per-case JSON

`run_matrix.sh` can also provision deterministic fixtures before verification:

```bash
# Generate stub fixtures for all three engines, then run one scenario.
bash tests/e2e/apps-matrix/run_matrix.sh --install --scenario wordpress-core

# Generate fixtures only.
bash tests/e2e/apps-matrix/run_matrix.sh --install-only --scenario wordpress-core

# Fast subset from MANIFEST.yaml.
bash tests/e2e/apps-matrix/run_matrix.sh --tier pr --install

# Explicit case subset.
bash tests/e2e/apps-matrix/run_matrix.sh --scenario wordpress-core --case WP_CORE_002
```

Default engine URLs:

- `APACHE_URL` / `MATRIX_APACHE_URL`: `http://localhost:18080`
- `OLS_MODULE_URL` / `MATRIX_OLS_MODULE_URL`: `http://localhost:38080`
- `OLS_NATIVE_URL` / `MATRIX_OLS_NATIVE_URL`: `http://localhost:28080`

Useful knobs:

- `MATRIX_OUT_DIR`: report/artifact directory
- `MATRIX_ENGINES`: comma-separated engine list; must include `apache,ols_module`
- `MATRIX_CURL_TIMEOUT`: per-request timeout in seconds, default `20`
- `MATRIX_TTL_SKEW`: allowed TTL assertion skew in seconds, default `5`
- `MATRIX_ALLOW_FAILURES=1`: keep runner exit code at 0 while collecting failures
- `MATRIX_FIXTURE_MODE=stub|overlay|real`: fixture provisioning mode, default
  `stub`
- `MATRIX_FIXTURE_ROOT`: generated docroot root, default under `MATRIX_OUT_DIR`
- `MATRIX_APACHE_DOCROOT`, `MATRIX_OLS_MODULE_DOCROOT`, `MATRIX_OLS_NATIVE_DOCROOT`:
  custom docroots for overlaying probes/assets into real engines
- `MATRIX_REAL_ALLOW_NETWORK=1`: allow real fixture installers to download app
  sources
- `MATRIX_REAL_RUN_APP_INSTALL=1`: run app-level install commands where
  supported. WordPress uses WP-CLI; Laravel runs key/migration setup; Drupal,
  Nextcloud, Joomla, and MediaWiki run their database-backed CLI installers.
- `MATRIX_REAL_DEEP_CONFIG=1`: apply app/plugin-specific post-install
  configuration and write `.apps-matrix/app_config.json`, default `1`.
- `MATRIX_REAL_VALIDATE_CONFIG=1`: validate the app config manifest after
  provisioning, default `1`.
- `MATRIX_REAL_STRICT_PLUGIN_INSTALL=auto|0|1`: require WordPress plugin
  directories to exist after app install. `auto` is the default and enables the
  check for WordPress plugin scenarios while leaving WordPress core and
  non-WordPress apps unchanged.
- `MATRIX_REAL_HEALTH_PROBES=1`: run app-native post-install health probes
  where supported, default `1`.
- `MATRIX_REAL_ENFORCE_HEALTH_PROBES=1`: fail provisioning if a health probe
  fails, default `1`.
- `MATRIX_REAL_DOWNLOAD_DIR`: archive/plugin download cache, default under
  `MATRIX_OUT_DIR`
- `MATRIX_REAL_DB_INSTALL_HOST` / `MATRIX_REAL_DB_INSTALL_PORT`: DB address used
  by local installer commands
- `MATRIX_REAL_DB_RUNTIME_HOST` / `MATRIX_REAL_DB_RUNTIME_PORT`: DB address
  written into app config for web containers; `run_full_matrix.sh` defaults this
  to the compose service endpoint `db:3306`
- `MATRIX_PROBE_MODE=enabled|local-only|disabled`: controls `_probe` exposure,
  default `enabled` for test runs
- `MATRIX_PROBE_OVERWRITE=1`: allow replacing an existing unmanaged `_probe`
  directory in overlay/real mode
- `MATRIX_READY_PATH`: compose readiness probe path, default
  `/_probe/server.php`. Set this to `/` only when you intentionally want
  readiness to depend on the app homepage instead of the harness PHP probe.
- `MATRIX_READY_PATH_DRUPAL`: Drupal-specific readiness probe path, default
  `/core/misc/drupal.js`, because stock Drupal `.htaccess` denies
  non-whitelisted PHP files such as `/_probe/server.php`, while the homepage can
  depend on full Drupal PHP bootstrap.
- `MATRIX_PER_ENGINE_BUDGET_MS`: optional per-request budget recorded in
  `performance.csv`
- `MATRIX_ENFORCE_PERF_BUDGET=1`: fail cases that exceed
  `MATRIX_PER_ENGINE_BUDGET_MS`
- `MATRIX_SCENARIO_BUDGET_MS`: optional scenario budget recorded in
  `full-matrix-timing.csv`
- `MATRIX_ENFORCE_SCENARIO_BUDGET=1`: fail scenarios that exceed
  `MATRIX_SCENARIO_BUDGET_MS`
- `MATRIX_DB_DUMP_DIR`: database dump handoff directory used by
  `MATRIX_FULL_PHASE=install|verify`, default `MATRIX_OUT_DIR/db-dumps`
- `MATRIX_ARTIFACT_MODE=summary|full`: artifact manifest mode written by
  `run_full_matrix.sh`, default `summary`

Fixture modes:

- `stub`: creates deterministic synthetic app docroots, probes, route maps,
  assets, and generated `.htaccess` files. This is used by harness smoke tests.
- `overlay`: copies probes, seeds deterministic fixture assets, and captures
  the current `.htaccess` without replacing a real installed app.
- `real`: downloads pinned app sources, seeds deterministic app/plugin assets,
  then overlays probes/assets and captures `.htaccess`. When network is disabled
  it writes an installer plan and falls back to deterministic docroot generation
  so CI can still validate the full runner shape.

The real installers pin default versions in `config/versions.env`. They are
designed to be deterministic. Heavyweight app setup remains opt-in because it
needs PHP CLI, Composer for Composer-based apps, MySQL/MariaDB client tools, and
an available database. Installer output is written under
`MATRIX_OUT_DIR/real-install-logs`; command headers redact database and admin
password arguments before logs are uploaded.

Real provisioning also writes:

- `real-install-cache.csv`: downloaded archive cache hit/miss and byte counts
- `real-install-steps.csv`: installer step timings and log paths
- `real-install-health.csv`: app-native health probe result rows
- `real-install-health/<scenario>/*.json`: health probe details
- `.apps-matrix/app_config.json`: app-specific config/check manifest per docroot
- `.apps-matrix/laravel_config.json`: password-free Laravel DB/config snapshot
  copied into the public docroot artifact
- `performance.csv`: per-case/per-engine request timings
- `full-matrix-timing.csv`: scenario-level install/verify timings
- `budget-suggestions.env`: tuned budget suggestions from collected timings
- `artifact-manifest.txt`: relative artifact list for summary/full upload modes
- `db-dumps/<scenario>.sql.gz`: database handoff dump when
  `MATRIX_REAL_RUN_APP_INSTALL=1`

`Dockerfile.fixture` provides a cacheable fixture-builder image with PHP,
Composer, MySQL client tools, and archive utilities. CI/release-gate jobs can
cache `MATRIX_REAL_DOWNLOAD_DIR` and Composer packages between explicit runs.
The image defaults to `prewarm_fixture_cache.sh`, which can prefetch pinned app
archives and Composer dependencies for
`MATRIX_SCENARIOS=heavy|wordpress|cms|framework`.

## Scenario List

Matrix scenarios:

1. `wordpress-core`
2. `wordpress-w3-total-cache`
3. `wordpress-litespeed-cache`
4. `wordpress-wordfence`
5. `wordpress-redirection`
6. `wordpress-ewww`
7. `wordpress-wp-optimize`
8. `drupal`
9. `laravel`
10. `nextcloud`
11. `joomla`
12. `mediawiki`

## Layout

- `run_matrix.sh`: top-level scenario runner
- `lib/matrix_common.sh`: common helpers and report utilities
- `lib/run_cases.py`: request executor, assertion engine, and report writer
- `lib/result_schema.json`: output schema for per-case results
- `scenarios/<name>/cases.yaml`: declarative case list
- `scenarios/<name>/install.sh`: app install/bootstrap contract
- `scenarios/<name>/verify.sh`: scenario runner contract
- `scenarios/<name>/notes.md`: scenario-specific coverage notes
- `out/`: reports and artifacts

## Runner Modes

Runner supports:

- `--list`: list known scenarios
- `--scenario <name>`: run a specific scenario
- `--case <id>`: run an explicit case subset
- `--tier pr|nightly|all`: select scenarios/cases from `MANIFEST.yaml`
- `--install`: run provisioning before verification
- `--install-only`: run provisioning without verification
- `--plan-only`: print execution plan without attempting install/provision

## Harness Smoke

`run_fixture_smoke.sh` validates the harness without Apache/OLS by serving
generated fixture route maps through a tiny Python HTTP server:

```bash
bash tests/e2e/apps-matrix/run_fixture_smoke.sh
```

This is the CI-safe check for install orchestration, probe deployment, case
filtering, HTTP assertions, report generation, and JSON result validation.

## Full Matrix

`run_full_matrix.sh` provisions real scenarios, starts Apache, OLS native, and
OLS module containers with mounted docroots, runs each scenario, captures
container logs, and validates results:

```bash
MATRIX_SCENARIO=wordpress-core \
MATRIX_FIXTURE_MODE=real \
MATRIX_REAL_ALLOW_NETWORK=1 \
bash tests/e2e/apps-matrix/run_full_matrix.sh
```

Use `MATRIX_SCENARIOS=all`, `heavy`, `wordpress`, `cms`, `framework`, or a
comma-separated list to run more than one scenario locally. The manual heavy
workflow can run all 12 scenarios as a GitHub Actions matrix and upload one
artifact per scenario.

`MATRIX_FULL_PHASE=install|verify|all` can split source/app provisioning from
the engine run. `install` writes docroot fixtures and `db-dumps/<scenario>.sql.gz`;
`verify` restores the dump and starts the web engines. `all` is the default.
`MATRIX_COMPOSE_BUILD=0` disables `docker compose --build` and uses the configured
images as-is. Pair it with `MATRIX_APACHE_IMAGE=<registry/image:tag>` and
`MATRIX_OLS_MODULE_IMAGE=<registry/image:tag>` to run release-gate checks against
prebuilt Apache and patched OLS runtimes instead of compiling them on every
matrix job.

Real CMS fixtures may include Apache front-controller directives such as
`FallbackResource /index.php` and `Header onsuccess ...`; these are parsed by
the module path so Drupal-style stock `.htaccess` files do not fail readiness
on unsupported syntax before the case-level comparison runs.

Budget suggestions can be generated from collected artifacts:

```bash
python3 tests/e2e/apps-matrix/lib/tune_budgets.py \
  --out-dir tests/e2e/apps-matrix/out/full-matrix \
  --include-per-engine
```

To ratchet checked-in defaults after enough CI/VPS samples exist:

```bash
MATRIX_RATCHET_MIN_SAMPLES=5 \
MATRIX_RATCHET_REQUIRE_SCENARIOS=heavy \
MATRIX_RATCHET_REQUIRE_PHASES=all \
MATRIX_RATCHET_UPDATE_ENV=1 \
scripts/apps-matrix-ratchet-budgets.sh tests/e2e/apps-matrix/out/full-matrix
```

`scripts/apps-matrix-ratchet-budgets.sh` refuses to update
`config/budgets.env` until every required scenario/phase has the configured
number of successful samples. Only enable workflow/DO `enforce_budgets=1` after
that ratchet step passes against representative p95 data.

The release-facing policy lives in `config/budget-policy.env` and is validated
by `scripts/check-apps-matrix-budget-policy.sh`. Keep the policy in `advisory`
mode for releases that do not yet have enough p95 samples; switch it to
`enforced` only after `scripts/apps-matrix-ratchet-budgets.sh` has updated
`config/budgets.env` from representative successful CI/DO artifacts.

## Runtime Image Prebuilds

`.github/workflows/apps-matrix-runtime-images.yml` builds the two shared
runtime images once per commit SHA and pushes them to GHCR:

- `apps-matrix-apache-runtime:<sha>`
- `apps-matrix-ols-module:<sha>`

The workflow resolves image refs once, then builds the Apache and patched OLS
images in parallel. Both builds use Docker BuildKit `type=gha` cache. By
default `reuse_existing=1` makes the workflow inspect every requested GHCR tag
and skip a runtime build when all tags already exist, which makes repeated
release-gate or heavy workflow attempts for the same SHA avoid rebuilding large
images. Set `reuse_existing=0` only when intentionally refreshing a tag.

The patched OLS image uses the LiteSpeed runtime image and official `lsphp`
packages by default (`litespeedtech/openlitespeed:1.9.1-lsphp82` with
`APPS_MATRIX_LSPHP_VERSION=82`), so app-matrix runtime validation does not build
PHP from source. Full RPM/source compilation remains available for packaging
validation, not for every app scenario.

When the patched OLS image must be built locally, `tests/e2e/Dockerfile.custom-ols`
defaults to `OLS_RPM_BUILD_MODE=build`. That mode stops after the RPM `%build`
stage and copies the patched `openlitespeed` binary plus `litehttpd_htaccess.so`
from `litehttpd-artifacts`, avoiding the WebAdmin `admin_php` compile that the
runtime image does not copy. Use `--build-arg OLS_RPM_BUILD_MODE=package` only
when intentionally exercising the full RPM `%install` path.
The compose wrapper passes `MATRIX_OLS_BUILD_JOBS` and
`MATRIX_OLS_RPM_BUILD_MODE` through as Docker build args; the DO release-gate
script exposes the same knobs as `APPS_MATRIX_DO_OLS_BUILD_JOBS` and
`APPS_MATRIX_DO_OLS_RPM_BUILD_MODE`.

Manual heavy and DO release-gate workflows consume those images with
`MATRIX_COMPOSE_BUILD=0`, so scenario jobs pull the same runtime images instead
of rebuilding Apache and patched OLS independently. Leave
`runtime_images_reuse_existing` at `1` for reruns and set it to `0` only when a
pushed tag must be rebuilt intentionally.

## Heavy Real Install

`.github/workflows/apps-matrix-heavy.yml` is a manual workflow for the full
12-scenario heavy matrix with `MATRIX_REAL_RUN_APP_INSTALL=1`. The workflow
supports `phase=all`, `phase=install`, and `phase=split`;
`split` uploads install artifacts and restores them in a separate verify job.
It also supports health-probe enforcement, `strict_plugin_install=auto|0|1`,
budget enforcement, `family=all|wordpress|cms|framework`,
`prewarm_cache=0|1`, `compose_build=0|1`, optional `apache_image` and
`ols_module_image`, `prebuild_apache_image=0|1`,
`prebuild_ols_module_image=0|1`, `runtime_images_reuse_existing=0|1`, and
`artifact_mode=summary|full`.
By default the workflow builds one Apache runtime image and one patched
`ols_module` runtime image, pushes both to GHCR with BuildKit cache enabled, and
then runs scenario jobs with `MATRIX_COMPOSE_BUILD=0`. Supplying
`apache_image` or `ols_module_image` uses that external image instead; setting
the corresponding `prebuild_*_image=0` disables the one-shot prebuild for that
runtime. Repeated heavy runs reuse already-published one-shot tags by default;
set `runtime_images_reuse_existing=0` only to deliberately refresh them.
Use this workflow or a disposable external VPS when validating heavy
network/runtime behavior; normal PR/CI paths stay on deterministic fixtures and
nightly overlay coverage.

## DigitalOcean Reality Test

`scripts/apps-matrix-do-vps-test.sh` creates a disposable DigitalOcean droplet,
syncs the current checkout, runs a heavy real-install matrix, fetches artifacts,
and destroys the droplet by default:

```bash
DIGITALOCEAN_ACCESS_TOKEN=... \
APPS_MATRIX_DO_SCENARIOS=heavy \
APPS_MATRIX_DO_ENGINES=apache,ols_module \
bash scripts/apps-matrix-do-vps-test.sh
```

Artifacts are written to `tests/e2e/apps-matrix/out/do-vps` by default. The DO
runner defaults to `APPS_MATRIX_DO_ARTIFACT_MODE=summary` to avoid transferring
full app docroots; set it to `full` for manual debugging. Set
`APPS_MATRIX_DO_KEEP_DROPLET=1` only for manual debugging.

For release-gate runs, prefer prebuilt Apache and patched OLS runtime images:

```bash
DIGITALOCEAN_ACCESS_TOKEN=... \
APPS_MATRIX_DO_SCENARIOS=heavy \
APPS_MATRIX_DO_ENGINES=apache,ols_module \
APPS_MATRIX_DO_COMPOSE_BUILD=0 \
APPS_MATRIX_DO_APACHE_IMAGE=ghcr.io/example/litehttpd/apps-matrix-apache-runtime:release-candidate \
APPS_MATRIX_DO_OLS_MODULE_IMAGE=ghcr.io/example/litehttpd/apps-matrix-ols-module:release-candidate \
bash scripts/apps-matrix-do-vps-test.sh
```

`.github/workflows/apps-matrix-do-release-gate.yml` automates this path: it
first calls the runtime-image workflow, then runs the DO VPS test with
`APPS_MATRIX_DO_COMPOSE_BUILD=0`, registry login, and pre-pulled GHCR images.
Direct release-gate dispatches expose `runtime_images_reuse_existing`; callers
that pass both `apache_image` and `ols_module_image` skip the runtime-image build
job entirely.

For private registries, pass Docker credentials explicitly; the runner logs in
before syncing/running the matrix and pre-pulls both runtime images:

```bash
DIGITALOCEAN_ACCESS_TOKEN=... \
APPS_MATRIX_DO_DOCKER_REGISTRY=ghcr.io \
APPS_MATRIX_DO_DOCKER_USERNAME=... \
APPS_MATRIX_DO_DOCKER_PASSWORD=... \
APPS_MATRIX_DO_COMPOSE_BUILD=0 \
APPS_MATRIX_DO_APACHE_IMAGE=ghcr.io/example/litehttpd/apps-matrix-apache-runtime:sha \
APPS_MATRIX_DO_OLS_MODULE_IMAGE=ghcr.io/example/litehttpd/apps-matrix-ols-module:sha \
bash scripts/apps-matrix-do-vps-test.sh
```

If no prebuilt image is available, the runner can still build locally, but that
path compiles Apache PHP extensions, patched OLS, and the OLS static third-party
dependency set, so it is substantially slower than pulling prebuilt runtimes.
Recent DO reality runs measured the patched OLS/RPM image build at roughly
20 minutes by itself, including ModSecurity, third-party static libraries, and
admin PHP. Treat that local-build path as a fallback or packaging validation
path, not the default app-matrix verification path.
Newer local patched-OLS runtime builds use the build-only RPM stage by default;
that still compiles OLS and static third-party libraries, but skips WebAdmin PHP.
The full package install path remains available with `OLS_RPM_BUILD_MODE=package`
for release packaging validation.

Prepared DigitalOcean snapshots can remove the VPS cold-start install cost:

```bash
DIGITALOCEAN_ACCESS_TOKEN=... \
APPS_MATRIX_DO_APACHE_IMAGE=ghcr.io/example/litehttpd/apps-matrix-apache-runtime:sha \
APPS_MATRIX_DO_OLS_MODULE_IMAGE=ghcr.io/example/litehttpd/apps-matrix-ols-module:sha \
APPS_MATRIX_DO_DOCKER_REGISTRY=ghcr.io \
APPS_MATRIX_DO_DOCKER_USERNAME=... \
APPS_MATRIX_DO_DOCKER_PASSWORD=... \
bash scripts/apps-matrix-do-prepare-base-image.sh
```

The script installs Docker, Compose, PHP CLI, Composer, MySQL client tools, and
archive utilities, optionally pre-pulls runtime images, creates a snapshot, and
destroys the temporary droplet. Use the printed snapshot ID with:

```bash
APPS_MATRIX_DO_IMAGE=<snapshot-id>
APPS_MATRIX_DO_SKIP_DEP_INSTALL=1
APPS_MATRIX_DO_COMPOSE_BUILD=0
```

The harness is intentionally test-only. It does not modify product code.
