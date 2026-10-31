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
   an HMAC-SHA256 credential cookie is issued, and the query parameter
   is stripped — the visitor never sees it.
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

## Interaction with caching

Challenge pages are sent with `Cache-Control: no-store` and are never
produced for exempt UAs, so a reverse proxy or LSCache in front never
caches a challenge for a real user.
