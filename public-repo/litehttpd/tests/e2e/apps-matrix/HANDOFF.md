# Handoff Notes

This subtree is intentionally test-only. It does not modify module behavior.

## What Is Already Done

- scenario inventory for all 12 agreed app/plugin cases
- declarative `cases.yaml` per scenario
- scenario-local `install.sh` / `verify.sh` contracts
- shared result schema
- shared case contract documentation
- top-level runner that can enumerate and plan scenarios

## What The Next Implementation Model Should Build

1. Shared probe deployment
   - copy `probes/*.php` into each scenario's app docroot
   - verify probe paths do not conflict with app routing/security rules

2. Three-engine request executor
   - Apache reference
   - OLS with `litehttpd_htaccess`
   - native OLS
   - fetch headers/body/probe output per case and persist artifacts

3. Assertion engine
   - evaluate the assertion primitives from `CASES_SCHEMA.md`
   - derive TTL from `Cache-Control` / `Expires`
   - classify cases as `PASS_EQUIV`, `PASS_KNOWN_DIFF`, `FAIL_REGRESSION`, `FAIL_APP_BROKEN`

4. Reports
   - `summary.csv`
   - `known_diff.csv`
   - per-case JSON result matching `lib/result_schema.json`
   - per-engine artifact folders with headers/body/probe output

5. Fixture provisioning
   - fixed app/plugin versions
   - fixed database seeds
   - deterministic fixture files for deny and TTL paths
   - captured final `.htaccess`

## Non-Goals For This Subtree

- no product/module code changes
- no attempt to "fix" app behavior here
- no hidden pass conditions for known diffs
