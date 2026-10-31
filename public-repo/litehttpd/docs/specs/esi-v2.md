# Spec: ESI v2 — Variable substitution + gzip awareness

## Additions over v1
1. `<esi:vars>` content now has `$(VARIABLE)` substitution
2. gzip-compressed response bodies: inflate → ESI process → (optionally re-deflate)
3. Response header check: only process when Content-Type is text/html
   (or when X-LiteSpeed-Cache has ESI markers)

## Variable Substitution (esi:vars)
| Variable | Source |
|----------|--------|
| `$(HTTP_USER_AGENT)` | User-Agent request header |
| `$(HTTP_COOKIE)` | Cookie request header |
| `$(HTTP_REFERER)` | Referer request header |
| `$(HTTP_HOST)` | Host request header |
| `$(QUERY_STRING{...})` | Query string parameter |
| `$(REQUEST_METHOD)` | HTTP method |
| `$(REMOTE_ADDR)` | Client IP |
| Unknown `$(...)` | Empty string (W3C spec behavior) |

## Gzip Awareness
When Content-Encoding: gzip is present on the response:
1. Decompress chunks with zlib inflate (streaming, window = 32KB)
2. Run ESI state machine on decompressed data
3. Re-compress output with zlib deflate (level from original response, or 6)
4. If decompression fails, pass through original compressed bytes

Implementation: separate `esi_gzip_ctx_t` wrapping `z_stream`, feeding
decompressed bytes to `esi_process()` and compressing the output.

## Response Header Gating
Only engage ESI processing when:
- Content-Type includes "text/html", OR
- Response has `X-Esi: true` header (explicit opt-in)
Otherwise: bypass entirely (zero overhead)

## References
- W3C ESI Language Specification 1.0 §3.2 (esi:vars)
- W3C ESI Language Specification 1.0 §2.1 (esi:include attributes)
- zlib manual (inflateInit2 with windowBits=15+16 for gzip)
