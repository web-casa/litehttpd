# `cases.yaml` Contract

This file documents the declarative case format used by `apps-matrix`.

## Top-Level Fields

- `scenario`: stable scenario id
- `app`: human-readable fixture label
- `purpose`: short statement of what the scenario is meant to validate
- `cases`: ordered list of individual assertions

## Case Fields

- `id`: stable case id, unique within the scenario
- `category`: one of `routing`, `redirect`, `deny`, `headers`, `ttl`,
  `env`, `htaccess`, `app-health`
- `desc`: short description shown in reports
- `method`: HTTP method, usually `GET`
- `path`: request path to execute
- `probe_path`: optional alternate probe URL used to inspect routing/env state
- `notes`: optional extra guidance for fixture/setup behavior
- `assert`: ordered list of assertions

## Assertion Types

Current assertion primitives:

- `status_exact: 200`
- `status_in: 403,404`
- `location_exact: /target/path`
- `header_exact: Header-Name=value`
- `header_contains: Header-Name=substring`
- `header_absent: Header-Name`
- `body_contains: substring`
- `body_not_contains: substring`
- `probe_field_exact: KEY=value`
- `probe_field_contains: KEY=substring`
- `ttl_close: <seconds>`

`ttl_close` is intentionally approximate. Implementations should derive TTL
from `Cache-Control: max-age` or `Expires`, then compare engines within a small
allowed skew, normally `±5s`.

The shared executor treats Apache as the fixture/reference health check,
`ols_module` as the required compatibility target, and `ols_native` as a
visible known-difference target. A case where Apache and `ols_module` satisfy
the assertions but `ols_native` does not becomes `PASS_KNOWN_DIFF`.

## Deny Rules

Deny-style cases should target known-existing files or directories whenever
possible. A deny case is stronger when it verifies:

1. the path exists in the fixture,
2. Apache returns the expected denial behavior,
3. OLS variants do not silently fall through to the app homepage.

## Known Diff Policy

Cases may end in:

- `PASS_EQUIV`
- `PASS_KNOWN_DIFF`
- `FAIL_REGRESSION`
- `FAIL_APP_BROKEN`

`PASS_KNOWN_DIFF` is permitted only when:

- Apache is the reference engine,
- the app remains usable,
- the difference is recorded with a reason and risk level,
- the case is surfaced in the red-table report.

## Fixture Requirements

Every scenario implementation should provide:

- fixed app/plugin versions,
- captured final `.htaccess`,
- `_probe/*.php` endpoints,
- deterministic data needed for the case paths to exist.

The built-in `stub` fixture mode creates synthetic docroots and route maps for
harness validation. Real compatibility runs should use externally installed
apps plus `MATRIX_FIXTURE_MODE=overlay` so probes/assets are deployed while the
application-generated `.htaccess` remains authoritative.
