# Spec: LSCache .htaccess Bridge

## Architecture
```
[.htaccess] → [litehttpd module] → X-LiteSpeed-Cache-Control header → [OLS cache module] → [disk cache]
```

litehttpd does NOT implement caching itself. It sets the `X-LiteSpeed-Cache-Control`
response header from .htaccess directives. The OLS cache module (already loaded)
reads this header and handles storage/retrieval.

## How OLS Cache Module Works
1. On response: reads `X-LiteSpeed-Cache-Control` header
2. If value contains `public` or `private` + `max-age=N` → stores response
3. On subsequent request: checks cache before executing PHP
4. Cache hit → serves cached page (zero PHP execution)
5. Falls back to standard `Cache-Control` header if LiteSpeed header absent

## New .htaccess Directives (Apache mod_cache compatible)
| Directive | Value | Action |
|-----------|-------|--------|
| `CacheEnable` | `public` | Set `X-LSC-Cache: public` (response cacheable by all) |
| `CacheEnable` | `private` | Set `X-LSC-Cache: private` (browser-only cache) |
| `CacheDisable` | — | Set `X-LSC-Cache: off` (prevent caching) |
| `CacheMaxExpire` | `N` seconds | Override max-age in cache-control |
| `CacheDefaultExpire` | `N` seconds | Default max-age when not specified |
| `CacheIgnoreHeaders` | `header-list` | Headers to not include in cache key |
| `CacheIgnoreNoLastMod` | `On` | Cache responses without Last-Modified |
| `CacheStorePrivate` | `On` | Store private responses in server cache |
| `CacheStoreNoStore` | `On` | Store responses with no-store directive |

## Integration Point
In `on_send_resp_header()` hook:
1. Scan parsed directives for CacheEnable/CacheDisable
2. Construct `X-LiteSpeed-Cache-Control` header value
3. Call `lsi_session_add_resp_header()` to set it
4. OLS cache module (hook priority: later than ours) reads it

## WordPress Integration
The WordPress LSCache plugin also sets `X-LiteSpeed-Cache-Control` headers.
litehttpd's .htaccess directives take precedence (applied first, WP plugin
can override via PHP). If both set the header, the WP plugin's value wins
because PHP executes before our SEND_RESP_HEADER hook sees the final response.

## References
- OLS cache module source: `src/modules/cache/cache.cpp` (GPLv3)
- Apache mod_cache documentation (directive syntax)
- LSWS Enterprise CacheEnable documentation (behavioral reference)
