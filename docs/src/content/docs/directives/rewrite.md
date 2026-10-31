---
title: Rewrite
description: URL rewriting — native execution engine and directive reference
---

Rewrite rules **execute natively** on both stock and patched OpenLiteSpeed.
On patched OLS ("Full" edition) the LSI-API fast path runs; on stock OLS the
module's built-in engine executes an Apache mod_rewrite subset directly —
no patch required.

## Directives

| Directive | Syntax |
|-----------|--------|
| `RewriteEngine` | `RewriteEngine On\|Off` |
| `RewriteBase` | `RewriteBase /path/` |
| `RewriteCond` | `RewriteCond TestString Pattern [flags]` |
| `RewriteRule` | `RewriteRule Pattern Substitution [flags]` |
| `RewriteOptions` | `RewriteOptions inherit\|IgnoreInherit` |
| `RewriteMap` | `RewriteMap name txt\|rnd\|int:/path` |

:::note[Engine selection]
Patched OLS is detected automatically via an ABI marker and takes precedence.
On stock OLS the built-in engine runs. The engine in use is transparent —
the same `.htaccess` works on both.
:::

## Pattern Syntax (built-in engine)

- PCRE2 regular expressions (the same engine used everywhere in the module,
  with `match_limit` protection against pathological patterns)
- `.` matches newlines too (Apache rewrite semantics)
- A leading `!` negates the pattern — note that negated matches carry **no
  capture groups** for `$N` substitution

## Substitution

| Form | Behaviour |
|------|-----------|
| `-` | URI unchanged (use with `[F]`/`[G]` or condition-only rules) |
| `/path` | Internal rewrite |
| `/path?query` | Internal rewrite with new query string |
| `http(s)://…` | External redirect |
| relative | Prefixed with `RewriteBase` |

Expansions inside the substitution:

- `$0` … `$9` — capture groups from the rule pattern
- `%{VAR}` — server variables (same set as RewriteCond, below)

## RewriteRule Flags

| Flag | Effect |
|------|--------|
| `[L]`, `[END]` | Stop processing further rules |
| `[R]` / `[R=301..308]` | External redirect (default 302 when bare) |
| `[NC]` | Case-insensitive pattern |
| `[QSA]` | Append the original query string |
| `[F]` | 403 Forbidden (use `-` as substitution) |
| `[G]` | 410 Gone |
| `[NE]`, `[PT]` | Accepted, no-op in this subset |

## RewriteCond

Test strings may reference `%{VAR}` server variables:

`%{REQUEST_URI}`, `%{REQUEST_FILENAME}`, `%{QUERY_STRING}`, `%{HTTP_HOST}`,
`%{HTTPS}`, `%{REMOTE_ADDR}`, `%{REQUEST_METHOD}`, `%{HTTP_USER_AGENT}`,
`%{HTTP_REFERER}`, `%{HTTP:Header}`, `%{ENV:var}`

CondPattern forms:

| Form | Meaning |
|------|---------|
| PCRE2 regex | Matches the expanded test string |
| `!pattern` | Negation |
| `-f` / `-d` / `-s` | File exists / is directory / is non-empty (relative paths resolve against the docroot) |
| `<` `>` `=` | Lexicographic comparison with the literal operand |

Cond flags: `[NC]`, `[OR]` (an OR-group is satisfied when any member matches;
the default combines conditions with AND).

## Examples

### WordPress Permalinks

```apache
RewriteEngine On
RewriteBase /
RewriteRule ^index\.php$ - [L]
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule . /index.php [L]
```

### HTTPS Redirect

```apache
RewriteEngine On
RewriteCond %{HTTPS} off
RewriteRule ^(.*)$ https://%{HTTP_HOST}/$1 [R=301,L]
```

### Redirect with captures

```apache
RewriteEngine On
RewriteRule ^/old/(.*)$ /new/$1 [L]
```

### Block a path

```apache
RewriteEngine On
RewriteRule ^/secret - [F]
```

## Documented Divergences from Apache

- Rules run **once in file order**; after an internal rewrite the chain is
  not restarted (Apache's per-directory context re-scans from the top).
  The dominant real-world pattern — a single catch-all front controller —
  behaves identically.
- `$N` back-references inside `RewriteCond` test strings are approximated
  as empty until the first rule has matched.
- `%N` cond-group references inside substitutions are reserved but not
  filled in v1.

## GeoIP Conditions (zero extra code)

OLS ships native MaxMind MMDB support and injects `GEOIP_*` variables into
the request environment; this engine's `%{ENV:var}` reads them directly.
One-time server config (`httpd_config.conf`, exact syntax verified against
the OLS source):

```
geoipDB /usr/local/share/GeoLite2-Country.mmdb {
  geoipDBName COUNTRY_DB
  maxminddbenv GEOIP_COUNTRY_CODE COUNTRY_DB/country/iso_code
}
```

:::note[Two gotchas]
- The `maxminddbenv` value is a **single token** — `COUNTRY_DB/country/iso_code`
  with no space between the DB name and the path.
- Behind a proxy, set `useIpInProxyHeader 1` so the lookup uses the real
  client IP from `X-Forwarded-For` (only when the proxy is trusted).
:::

Then country rules are plain rewrites:

```apache
RewriteEngine On
RewriteCond %{ENV:GEOIP_COUNTRY_CODE} ^(US|DE)$ [NC]
RewriteRule .* - [F]
```

Verified live: GB-addressed request → 403, US → 200, no module changes
required.
