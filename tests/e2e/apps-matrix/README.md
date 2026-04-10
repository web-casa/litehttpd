# Apps Matrix E2E Plan

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

## Scenario List

Planned matrix:

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

This initial scaffold includes detailed starter specs for:

- `wordpress-core`
- `wordpress-redirection`
- `laravel`

## Layout

- `run_matrix.sh`: top-level scenario runner
- `lib/matrix_common.sh`: common helpers and report utilities
- `lib/result_schema.json`: output schema for per-case results
- `scenarios/<name>/cases.yaml`: declarative case list
- `scenarios/<name>/install.sh`: app install/bootstrap contract
- `scenarios/<name>/verify.sh`: scenario runner contract
- `scenarios/<name>/notes.md`: scenario-specific coverage notes
- `out/`: reports and artifacts

## Runner Modes

Current scaffold supports:

- `--list`: list known scenarios
- `--scenario <name>`: run a specific scenario
- `--plan-only`: print execution plan without attempting install/provision

The scaffold is intentionally test-only. It does not modify product code.
