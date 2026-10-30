---
title: ESI
description: Edge Side Includes — fragment inclusion, comment stripping and variable substitution
---

ESI processing is opt-in per directory with `EsiEnable`. When enabled,
response bodies of **handler output** (PHP and other dynamic handlers)
are filtered through the ESI engine; static files served from the OLS
sendfile fast path are not transformed.

## Directives

| Directive | Syntax | Default |
|-----------|--------|---------|
| `EsiEnable` | `On\|Off` | Off |

## Supported Tags

| Tag | Effect |
|-----|--------|
| `<esi:include src="/frag.html"/>` | Inline a fragment from the document root |
| `<esi:remove> … </esi:remove>` | Strip the enclosed content |
| `<esi:comment> … </esi:comment>` | Strip (comments never reach the client) |
| `<esi:vars> … </esi:vars>` | Substitute `$(VARIABLE)` references |

Supported variables inside `<esi:vars>`: `$(HTTP_USER_AGENT)`,
`$(HTTP_COOKIE)`, `$(HTTP_REFERER)`, `$(HTTP_HOST)`, `$(QUERY_STRING)`,
`$(REQUEST_METHOD)`, `$(REMOTE_ADDR)`. Unknown variables expand to an
empty string (W3C behaviour).

## Example

```apache
# .htaccess
EsiEnable On
```

```php
<?php
echo 'A<esi:include src="/frag.html"/>B';
echo '<esi:comment>internal note</esi:comment>C';
```

Response body the client receives (assuming `/frag.html` contains
`FRAGMENT`): `AFRAGMENTBC`.

## Include Security

`<esi:include>` is hardened against file disclosure:

- `src` must be an absolute path **inside the document root** — resolved
  with `realpath` containment, so intermediate symlinked directories cannot
  escape (an attacker-writable `uploads/` symlink aimed at `/etc` is
  refused)
- path traversal (`..`) rejected
- `O_NOFOLLOW` open; regular files only; 64 KB per fragment
- a missing or oversized fragment simply removes the tag — the page is
  still served

## Content-Type Gating

ESI applies to `text/html` and `application/xhtml+xml` responses only, and
never to compressed bodies. Other content types pass through untouched.

## Chunk-Boundary Correctness

Tags split across response chunks (streamed output) are reassembled before
processing; a page whose last chunk ends mid-tag is completed on the next
chunk or flushed literally at stream end. ESI markup never leaks to the
client as visible text.

## Dynamic Includes

An `src` that does not match a static file (or points at a handler such
as `.php`) is resolved as a **loopback sub-request**: the fragment runs
the full OLS pipeline — `.htaccess`, PHP/LSAPI, mod_security — and its
response body is spliced into the page. This is the LSWS Enterprise
dynamic-ESI behaviour.

```apache
EsiEnable On
```

```php
<?php echo 'Hello <esi:include src="/cart-summary.php"/>!'; ?>
```

`/cart-summary.php` executes with the parent's **Cookie** and **Host**
headers forwarded, so session-dependent fragments render correctly. Query
strings pass through (`/frag.php?n=7`).

Semantics and safeguards:

- **Hold-and-join**: output is retained from the first dynamic include
  and joined at response end — a page never streams out of order.
- **Hard budgets**: 1s per fragment, 2.5s per page (env
  `LITEHTTPD_ESI_DYN_TIMEOUT_MS` / `LITEHTTPD_ESI_DYN_TOTAL_MS`), 16
  fragments per page, 64 KB per fragment body. Every overflow removes the
  tag and completes the page (fail-open).
- **Loop protection**: fragment requests carry
  `X-LiteHttpd-Esi-Subrequest: 1` (visible to PHP in
  `$_SERVER`); a marked request refuses further *dynamic* includes —
  static ESI inside fragments still works.
- **Handler files are never inlined as source**: `.php` and friends skip
  the static fast path unconditionally.
- **Workers**: the joiner blocks briefly while a *different* OLS worker
  serves the fragment (with retries against connection-routing races).
  Run OLS with **Children ≥ 2**; on a single-worker instance every
  fragment times out and pages complete fail-open.
- The loopback target defaults to `127.0.0.1:80`; override with
  `LITEHTTPD_ESI_LOOPBACK=host:port`.
