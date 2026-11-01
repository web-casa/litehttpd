---
title: Bot Challenge
description: Self-hosted JS proof-of-work gate for bots and scrapers
---

`BotChallenge` is a fully self-hosted alternative to Cloudflare Turnstile
or OLS Enterprise's "managed challenge": clients without a valid
credential cookie get an inline HTML+JS challenge, solve a small
proof-of-work in the browser, and receive a signed cookie. No external
service, no JavaScript framework, works with JavaScript enabled — which
is exactly what most scrapers, credential-stuffers, and AI crawlers
don't have.

## Directives

| Directive | Syntax |
|-----------|--------|
| `BotChallenge` | `On \| Off` |
| `BotChallengeTTL` | `seconds` (default `3600`, minimum `60`) |
| `BotChallengeDifficulty` | `bits` (default `16`, range `8`–`24`) |
| `BotChallengeExempt` | `"regex"` matched against the User-Agent |
| `BotChallengeTrustedProxy` | `"cidr-list"` — honour X-Forwarded-For |
| `BotChallengePage` | `template.html` — custom challenge page |
| `BotChallengeIf` | `"ap_expr"` — condition the gate per request |

## Example

```apache
BotChallenge On
BotChallengeDifficulty 20
BotChallengeExempt "(?i)(googlebot|bingbot)"
```

- Difficulty 16 costs a browser a few milliseconds; 20 raises the
  per-request cost of solving to roughly 30× more hash attempts — a
  cheap dial against aggressive scrapers.
- Exempt verified crawler UAs you want crawling unimpeded. (For
  stricter verification, combine with a `RewriteCond %{HTTP_USER_AGENT}`
  + reverse-DNS style rules of your own.)

## How it works

1. A request arrives with no valid `lhttpd_bot` cookie (and the UA is
   not exempt): the module serves an inline challenge page — HTTP 200,
   `Cache-Control: no-store` — containing a daily salt and the
   difficulty.
2. The page's JavaScript loops FNV-1a (the same 32-bit hash the C
   verifier uses) over `salt:nonce` until the top *difficulty* bits are
   zero, then navigates to `<uri>?__lhc=<nonce>`.
3. That request reaches the gate: the nonce is verified in one pass,
   an HMAC-SHA256 credential cookie is issued, and the server answers
   with a **302 to the clean URL** (the `__lhc` parameter dropped, any
   other parameters kept). The browser re-requests with the fresh
   cookie and lands on a canonical, cacheable address — the
   credential-free `?__lhc=...` request never yields a cacheable 200.
4. Subsequent requests verify expiry + MAC (constant-time). The
   credential is bound to the client's IP class (IPv4 `/24`, IPv6
   first 4 groups), so a stolen cookie does not travel well.

## Operations

- **Shared secret**: `/var/run/litehttpd/botchallenge.secret` (0600,
  created automatically on first use, shared by all workers so cookies
  verify across workers). Override the path with the
  `LITEHTTPD_BOT_SECRET` environment variable.
- The RPM and tarball installs provision `/run/litehttpd` writable by
  the OLS worker user (tmpfiles.d covers reboots). If the secret can be
  neither read nor created, the gate keeps working with a weaker key
  and logs one warning — fix the directory permissions.
- Rotating the secret invalidates all cookies; visitors simply solve
  one more challenge.
- Tuning: `TTL` trades convenience against replay windows;
  `Difficulty` raises the per-request cost for everyone including you.

## Behind a reverse proxy / CDN

By default the credential binds to the DIRECT peer — behind Cloudflare
or an Nginx proxy that means every visitor shares the proxy's IP class,
which defeats the binding. `BotChallengeTrustedProxy` fixes that:

```apache
BotChallenge On
BotChallengeTrustedProxy "10.0.0.0/8 192.168.0.0/16"
```

When (and only when) the direct peer is inside the listed CIDRs, the
credential binds to the **rightmost** `X-Forwarded-For` token — the
address your proxy appended. Leftmost (client-supplied) entries are
spoofable and are never used; an unlisted peer's XFF is ignored
entirely. IPv4 CIDRs.

## Conditional gating

`BotChallengeIf` takes an `ap_expr` expression evaluated per request;
the gate only applies when it holds. Combined with the GeoIP pathway
this scopes the challenge by country:

```apache
BotChallenge On
# challenge everything except US/GB traffic
BotChallengeIf "%{ENV:GEOIP_COUNTRY_CODE} !~ /^(US|GB)$/"
```

Any expression works — URL, headers, environment. An unparsable
expression fails **open** (gate off) with one warning per occurrence in
the error log: a broken config never locks humans out.

## Custom challenge page

`BotChallengePage` serves your own template instead of the built-in
page — brand it, add CSS, localize it:

```apache
BotChallengePage challenge.html
```

The path is relative to the `.htaccess` directory (absolute paths must
resolve inside it; symlinks are not followed — same confinement as
`AuthUserFile`). The template may use `%{SALT}`, `%{DIFF}` and
`%{URI}`; the URI is JS-escaped before substitution. Your template
must run the same FNV-1a loop and navigate to `%{URI}?__lhc=<nonce>` —
copy the `<script>` block from the built-in page as a starting point.
A missing or invalid template falls back to the built-in page.

## Rotating the secret

`tools/rotate-botsecret.sh` (shipped in the tarball / repo) atomically
replaces the secret and gracefully restarts OLS so workers reload it.
All outstanding cookies are invalidated; visitors simply solve one more
challenge.

## Interaction with caching

Challenge pages are sent with `Cache-Control: no-store` and are never
produced for exempt UAs, so a reverse proxy or LSCache in front never
caches a challenge for a real user.
