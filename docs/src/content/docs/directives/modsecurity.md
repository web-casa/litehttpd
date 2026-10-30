---
title: ModSecurity Bridge
description: Per-directory WAF control with SecRuleEngine — an LSWS Enterprise feature on OpenLiteSpeed
---

`SecRuleEngine` in `.htaccess` gives per-directory ModSecurity control —
a LiteSpeed Enterprise feature that stock OpenLiteSpeed lacks. LiteHTTPD
bridges it to the OLS `mod_security` module.

## Directives

| Directive | Syntax | Effect |
|-----------|--------|--------|
| `SecRuleEngine` | `On` | No-op — the engine keeps its configured state (default) |
| `SecRuleEngine` | `Off` | WAF bypassed for every request under this directory |
| `SecRuleEngine` | `DetectionOnly` | Not expressible per-request through libmodsecurity; logged and treated as `On` (fails toward protection) |

## Example

Disable the WAF for one legacy plugin endpoint that trips a rule:

```apache
# /wp-content/plugins/legacy-form/.htaccess
SecRuleEngine Off
```

Everything outside that directory keeps full WAF inspection.

## How it works

OLS's own `mod_security` module bypasses a request entirely when the
request environment variable `modsecurity` equals `off` — the same
mechanism the OLS cache module uses for cache-served hits. LiteHTTPD sets
that variable from an **early URI hook** that runs before the WAF's own
hook, so the bypass wins the race deterministically.

Merging follows Apache per-directory semantics: the deepest
`SecRuleEngine` in the directory chain wins (a child `On` overrides a
parent `Off`).

## Verified behaviour

Against OLS 1.9.4 with libmodsecurity 3.0.15 and a live SQLi rule
(`SecRule ARGS … deny`):

| Request | Result |
|---------|--------|
| `/?s=1' OR '1'='1` (no directive) | **403** — rule fires |
| Same request under `SecRuleEngine Off` | **200** — bypassed |

## Limitations

- `SecRuleRemoveById` and per-directory rule exclusions have no per-request
  surface in libmodsecurity and are not bridged.
- Requires the OLS `mod_security` module to be installed and enabled in
  server config; without it the directive is accepted but has nothing to
  bypass.
