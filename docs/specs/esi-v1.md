# Spec: ESI v1 — Tag State Machine (P7)

## Scope
Implements W3C ESI Language Specification 1.0 subset:
- `<esi:remove>` ... `</esi:remove>` — strip content (content between tags removed)
- `<esi:comment>...</esi:comment>` — strip (same as remove, for comments)
- `<esi:vars>...</esi:vars>` — variable substitution inside content
- `<esi:include src="..."/>` — deferred to v2 (needs subrequest)

## Architecture
Stream filter on `LSI_HKPT_RECV_RESP_BODY`:
1. Each chunk appended to module-private accumulation buffer
2. State machine scans for tag boundaries
3. Content outside ESI tags → `stream_write_next()` immediately (zero overhead
   for non-ESI responses)
4. Content inside remove/comment tags → discarded
5. Content inside vars tags → passthrough unchanged (v1; substitution in v1.1)
6. On `RCVD_RESP_BODY` → flush remaining, free buffer

## State Machine
```
STATE_PASSTHROUGH → see "<esi:" → STATE_TAG_OPEN
STATE_TAG_OPEN → identify tag type:
    "remove" or "comment" → STATE_STRIP (discard until matching close tag)
    "vars" → STATE_VARS (substitute until </esi:vars>)
    "include" → STATE_INCLUDE_V2 (not implemented, passthrough in v1)
    other → STATE_PASSTHROUGH (unknown tag, write as-is)
STATE_STRIP → see "</esi:remove>" or "</esi:comment>" → STATE_PASSTHROW
STATE_VARS → see "</esi:vars>" → STATE_PASSTHROUGH
```

## Buffer Management
- Max buffer: 1 MB (configurable)
- Chunk-by-chunk streaming: flush passthrough content immediately
- Only hold content between ESI tags (remove content is discarded, vars
  content is typically short)
- Bounded memory: if buffer exceeds limit, flush as passthrough and log warn

## Bypass Detection (Zero Overhead for Non-ESI)
- First 4KB scanned for "<esi:" — if absent, disable filter for this response
- Re-enable on HANDLER_RESTART (internal redirect may produce ESI)

## Variable Substitution (esi:vars)
Supports `$(HTTP_HEADER_NAME)` syntax per W3C spec:
- `$(HTTP_USER_AGENT)` → User-Agent header value
- `$(HTTP_COOKIE)` → Cookie header value
- `$(QUERY_STRING)` → query string
- Unknown variables → empty string (spec-compliant)

## References
- W3C ESI Language Specification 1.0 (public)
- OLS modgzip stream filter pattern (GPLv3, behavioral reference only)
