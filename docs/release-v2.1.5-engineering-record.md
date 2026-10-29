# litehttpd v2.1.5 Engineering Record

Date: 2026-07-06

This document records the local engineering details for the v2.1.5 release
cycle and the plan for making the next release faster and cheaper. It is a
local engineering note, not release marketing copy.

## Final State

- Private release tag: `v2.1.5`
- Release tag commit: `e0be3bd3ea7bea259f81d47ee6a151fcea7f2e2e`
- Post-release private sync commit: `fc7849aba45843e6ac553ec54eb19e0d3b623ecb`
- Public mirror commit: `98a00f68ac64ebfb170dda5200aea0f4211af6bd`
- Private release URL: `https://github.com/web-casa/litehttpd-private/releases/tag/v2.1.5`
- Private release workflow run: `28780242435`
- DO release-gate run: `28779181046`
- Runtime images workflow run: `28778305290`
- Final release-gate CI run: `28778278916`
- Final release-gate CodeQL run: `28779444786`
- Post-release sync CI run: `28783038345`
- Post-release sync CodeQL run: `28783038326`

The release is published, not a draft, with 16 assets. There are no arm64 or
aarch64 release assets. The final release tag intentionally remains on
`e0be3bd`; the later `fc7849a` commit only synchronizes the public mirror and
does not change the shipped release artifacts.

## Scope Developed

### Apps Matrix

- Added deeper real-app semantic checks for Drupal, Nextcloud, Joomla, and
  MediaWiki.
- Added semantic snapshot writers in
  `tests/e2e/apps-matrix/lib/real_install_common.sh`.
- Extended `tests/e2e/apps-matrix/lib/real_app_config.py` with JSON value
  checks for app-level config and install state.
- Added app family selection in `run_full_matrix.sh`:
  - `wordpress`
  - `cms`
  - `framework`
  - `heavy`
- Added fixture cache prewarm support via
  `tests/e2e/apps-matrix/prewarm_fixture_cache.sh`.
- Added fixture/runtime Dockerfile improvements for prebuilt runtime paths.
- Added artifact manifest support for `scenario-selection.txt`, making it
  clear which scenarios, engines, phases, and runtime image tags were used.

### CI and Release Gates

- Removed scheduled Nightly as a release dependency to reduce cost and avoid
  unnecessary recurring runs.
- Formal release gates are now:
  - CI
  - CodeQL
  - Apps Matrix Runtime Images
  - DO Release Gate
- `scripts/check-github-release-gates.sh` now checks the active formal gates
  without Nightly.
- `scripts/apps-matrix-final-do-gate.sh` now dispatches the DO release-gate
  workflow directly instead of relying on Nightly.
- `scripts/check-release-tag-gate.sh` now checks
  `apps-matrix-do-release-gate.yml` artifacts instead of Nightly artifacts.
- `scripts/check-release-scope.sh` was adjusted to accept the current workflow
  set after Nightly removal.

### x86_64-only Release

- Removed ARM64/QEMU release expectations from the release matrix.
- `scripts/check-release-metadata.sh` now validates x86_64 release-note
  patterns instead of old arch placeholders.
- `scripts/check-release-matrix.sh` now asserts:
  - no ARM64 release jobs
  - no QEMU setup
  - no ARM64 release artifact references
  - RPM repo publishing loops over `x86_64` only

### Release Asset Hardening

- Fixed stale draft-release assets by deleting all existing draft release
  assets before uploading new ones.
- Added a release-matrix guard requiring `gh release delete-asset` in the
  release workflow.
- This fixed the concrete failure where old aarch64 draft assets polluted the
  x86_64-only release and broke `publish-rpm-repo`.
- Final assets were downloaded and validated:
  - `SHA256SUMS` passed for all files.
  - `RELEASE_REQUIRE_FULL_ARTIFACTS=1 bash scripts/check-release-artifacts.sh`
    passed.
  - No arm64/aarch64 asset names were present.

### Public Mirror Sync

- Synced the public mirror after the release:
  - private post-release sync: `fc7849a public: sync release mirror`
  - public repo commit: `98a00f6 Sync litehttpd release mirror`
- Fixed `tests/check_patches.sh` so it works in the public mirror, where
  private release packaging files are intentionally absent.
- Fixed `.gitignore` allowlists so public mirror test library files under
  `public-repo/litehttpd/tests/e2e/lib/` and
  `public-repo/litehttpd/tests/e2e/apps-matrix/lib/` are tracked.
- `bash scripts/check-public-release-sync.sh --ref HEAD --strict` passed after
  the mirror sync commit.

### Documentation

- Updated private docs under `docs/`.
- Updated and pushed public docs through `web-casa/litehttpd`.
- Verified the public docs build with Node 24:
  - Astro build completed successfully.
  - 143 pages were generated.
- Known non-blocking warning:
  - `apacheconf` code fences fall back to text highlighting because the
    highlighter does not know that language.

## Failures Found and Fixed

### DO and Apps Matrix Reality Testing

- DO reality tests initially exposed slow cold builds and repeated OLS/runtime
  image build problems.
- `Dockerfile.custom-ols` needed more careful ARG scoping for runtime image
  selection.
- LiteSpeed `lsphp82` package availability was narrower than assumed; required
  package suffixes were reduced to real packages and optional suffixes were
  treated as optional.
- DO API calls occasionally returned transient TLS EOF; the launcher gained a
  retry loop.
- `scenario-selection.txt` was added so artifacts prove whether `heavy` really
  expanded to all scenarios.
- Final DO release-gate used prebuilt GHCR runtime images and
  `MATRIX_COMPOSE_BUILD=0`, avoiding cold runtime rebuild inside the app
  matrix.

### Release Workflow

- A release run failed because a draft GitHub Release still contained stale
  aarch64 assets from earlier attempts.
- The release workflow now resets the draft release and deletes existing assets
  before new uploads.
- The final release workflow succeeded after this fix.

### Local Artifact Validation

- A full `gh release download` was interrupted while large OLS artifacts were
  still partial on disk.
- `sha256sum -c SHA256SUMS` caught the truncated files.
- The large OLS RPM/tarball assets were redownloaded individually and then
  passed checksum and artifact-matrix validation.

## Validation Completed

Static and local checks used during the cycle included:

- `bash -n` for modified shell scripts.
- `shellcheck` for modified shell scripts.
- `python3 -m py_compile` for Apps Matrix helper Python files.
- YAML parsing for workflow changes.
- `git diff --check` for whitespace.
- `bash tests/check_patches.sh`
- `bash tests/check_consistency.sh`
- Public mirror:
  - `git diff --check`
  - `bash tests/check_patches.sh`
  - `bash tests/check_consistency.sh`
  - Astro docs build with Node 24
- Release artifacts:
  - `sha256sum -c SHA256SUMS`
  - `RELEASE_REQUIRE_FULL_ARTIFACTS=1 bash scripts/check-release-artifacts.sh`
- Release gate:
  - `bash scripts/check-release-tag-gate.sh --repo web-casa/litehttpd-private --ref e0be3bd3ea7bea259f81d47ee6a151fcea7f2e2e --tag v2.1.5 --allow-existing-tag --require-do-gate`

The final tag gate confirmed:

- release source is isolated and clean
- metadata is consistent:
  - version `2.1.5`
  - OLS `1.9.1`
  - module `0.2.2`
  - directives `84`
- GitHub release gates are green
- DO release-gate artifact is present
- budget enforcement remains disabled because samples are insufficient

## DO Release-Gate Result

The final DO release-gate passed all 12 heavy scenarios with `apache` and
`ols_module`, using prebuilt runtime images and `MATRIX_COMPOSE_BUILD=0`.

Scenarios:

- `wordpress-core`
- `wordpress-redirection`
- `wordpress-w3-total-cache`
- `wordpress-litespeed-cache`
- `wordpress-wordfence`
- `wordpress-ewww`
- `wordpress-wp-optimize`
- `laravel`
- `drupal`
- `nextcloud`
- `joomla`
- `mediawiki`

Observed timings from the successful DO artifact:

- `wordpress-core`: 38080 ms
- `wordpress-redirection`: 33005 ms
- `wordpress-w3-total-cache`: 35498 ms
- `wordpress-litespeed-cache`: 33224 ms
- `wordpress-wordfence`: 35803 ms
- `wordpress-ewww`: 33010 ms
- `wordpress-wp-optimize`: 33647 ms
- `laravel`: 37890 ms
- `drupal`: 58667 ms
- `nextcloud`: 113457 ms
- `joomla`: 29675 ms
- `mediawiki`: 56465 ms

All failures CSV output was empty except for the header. Health and semantic
checks passed. DO droplet cleanup was verified after the run; no droplets were
left running.

## Remaining Policy State

### Budget Enforcement

Budget enforcement remains advisory. This is deliberate.

Reason:

- `config/budget-policy.env` requires at least 5 OK samples per heavy scenario
  and phase.
- The current release does not have enough successful p95 samples to safely
  ratchet budgets.

Next action after enough samples:

```bash
bash scripts/apps-matrix-ratchet-budgets.sh
```

Only after stable samples should `config/budgets.env` be ratcheted and
enforcement enabled.

## Faster and Cheaper Next Release Plan

The slowest and most expensive work this cycle was repeatedly building OLS,
runtime images, and real app install environments. The next release should be
structured so expensive work runs once, is promoted by SHA, and is reused by
later gates.

### 1. Keep the x86_64-only Release Matrix

Do not reintroduce ARM64/QEMU unless there is a hard release requirement.

Reasons:

- QEMU builds are slow and expensive.
- GitHub paid minutes do not automatically mean native ARM runners.
- The current release matrix and artifact checks are cleanly x86_64-only.

Required guardrails:

- Keep `scripts/check-release-matrix.sh` asserting no ARM64 and no QEMU.
- Keep release asset names explicit with `x86_64`.

### 2. No Scheduled Nightly

Keep Nightly removed.

Use manual or release-gate dispatch only:

- Runtime image build when a release candidate SHA changes.
- DO release-gate when runtime images are already built.
- Full release workflow only after CI, CodeQL, runtime images, and DO gate are
  green.

This avoids paying for repeated 12-scenario reality tests when no release is
being prepared.

### 3. Promote Runtime Images by Full SHA

The successful path is:

1. Build GHCR runtime images for the exact release SHA.
2. Run Apps Matrix with `MATRIX_COMPOSE_BUILD=0`.
3. Pull:
   - `apps-matrix-apache-runtime:<full-sha>`
   - `apps-matrix-ols-module:<full-sha>`

Do not let the DO release-gate rebuild OLS/runtime images unless explicitly
debugging runtime image creation.

### 4. Use DO Only as a Release Gate

DO should verify real VPS behavior, not act as a general CI runner.

Recommended default:

```bash
bash scripts/apps-matrix-final-do-gate.sh \
  --repo web-casa/litehttpd-private \
  --ref <release-sha> \
  --scenarios heavy \
  --engines apache,ols_module \
  --image-tag <release-sha> \
  --compose-build 0 \
  --watch
```

Use smaller families only while debugging:

- `wordpress`
- `cms`
- `framework`

Then run `heavy` once as the release gate.

### 5. Prepare a DO Base Snapshot

The remaining DO overhead is cold VPS provisioning. Build and maintain a base
snapshot with:

- Docker and Compose plugin
- GitHub CLI
- doctl
- jq, rsync, curl, git, Python
- any stable OS-level dependencies used by the Apps Matrix launcher

Expected usage:

```bash
bash scripts/apps-matrix-do-prepare-base-image.sh

APPS_MATRIX_DO_IMAGE=<snapshot-id> \
APPS_MATRIX_DO_SKIP_DEP_INSTALL=1 \
bash scripts/apps-matrix-do-vps-test.sh
```

This avoids reinstalling dependencies on every release-gate run.

### 6. Split Validation Into Cheap and Expensive Layers

Suggested order:

1. Cheap local/static gates:
   - shell syntax
   - shellcheck
   - Python compile
   - YAML parse
   - `git diff --check`
   - `tests/check_patches.sh`
   - `tests/check_consistency.sh`
   - release metadata/matrix checks
2. GitHub CI and CodeQL.
3. Runtime image workflow.
4. DO release-gate with prebuilt images.
5. Tag gate.
6. Release workflow.
7. Public mirror sync.

Do not start the release workflow until steps 1-5 are green.

### 7. Avoid Revalidating Large Artifacts the Slow Way

For final asset validation:

- Download by explicit asset name when possible.
- Check size from GitHub asset metadata before checksum.
- Then run `sha256sum -c SHA256SUMS`.
- Then run `check-release-artifacts.sh`.

Avoid relying on one long `gh release download` command for all assets; it can
leave partial files if interrupted and does not make progress clear.

### 8. Keep Public Sync After Release

The public mirror sync is important but should remain separate from the shipped
release tag.

Recommended flow:

1. Publish private release at the validated release SHA.
2. Run `public-repo/sync.sh`.
3. Run `check-public-release-sync.sh --ref HEAD --strict`.
4. Push private post-release mirror snapshot.
5. Push public repo mirror commit.

Do not move the release tag only to include public mirror bookkeeping.

### 9. Add Cost Controls to Workflows

Future workflow hardening should include:

- `concurrency` groups with `cancel-in-progress` for non-release candidate
  runs.
- Manual dispatch for expensive reality tests.
- Short artifact retention for intermediate matrix artifacts.
- Clear workflow names and run summaries that show:
  - release SHA
  - image tags
  - scenario family
  - engines
  - `MATRIX_COMPOSE_BUILD`

### 10. Capture Performance Samples Opportunistically

Do not run heavy tests just to gather budget samples. Instead:

- Keep successful release-gate and manually dispatched heavy artifacts.
- Ratchet only after at least 5 OK samples per scenario/phase.
- Keep enforcement advisory until p95 is stable.

## Recommended Next Release Checklist

Use this as the default fast path:

1. Create or select release candidate SHA.
2. Run local/static checks.
3. Push candidate to private main/release branch.
4. Run GitHub formal gates:

   ```bash
   bash scripts/check-github-release-gates.sh \
     --repo web-casa/litehttpd-private \
     --ref main \
     --sha <release-sha> \
     --rerun-formal \
     --watch
   ```

5. Confirm runtime images exist for `<release-sha>`.
6. Run DO release-gate with prebuilt images and `compose_build=0`.
7. Run tag gate:

   ```bash
   bash scripts/check-release-tag-gate.sh \
     --repo web-casa/litehttpd-private \
     --ref <release-sha> \
     --tag v<version> \
     --allow-existing-tag \
     --require-do-gate
   ```

8. Push or move the release tag only after gates are green.
9. Let Release workflow publish assets.
10. Download and verify final release assets:

    ```bash
    sha256sum -c SHA256SUMS
    RELEASE_REQUIRE_FULL_ARTIFACTS=1 \
      bash scripts/check-release-artifacts.sh <asset-dir>
    ```

11. Run public mirror sync as a post-release step.
12. Verify no DO droplets remain:

    ```bash
    doctl compute droplet list --format ID,Name,Status,PublicIPv4 --no-header
    ```

## Practical Target for Next Release

With the current fast path, the target should be:

- No repeated OLS cold builds inside Apps Matrix.
- No ARM64/QEMU time.
- No scheduled Nightly spend.
- One runtime image build per release candidate SHA.
- One full DO heavy release-gate run per final candidate.
- One release workflow run after all gates are green.

If this discipline is kept, the next release should spend most time only on
the unavoidable release artifact build and the single final reality gate,
instead of repeatedly rebuilding the same runtime and test environment.
