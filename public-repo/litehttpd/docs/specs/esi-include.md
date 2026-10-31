# Spec: ESI <esi:include> — Static File Include (v1)

## Architecture
```
[response body] → [ESI state machine detects <esi:include src="..."/>]
                        ↓
                 [map src to docroot + path]
                        ↓
                 [read file from disk (O_NOFOLLOW)]
                        ↓
                 [substitute include tag with file content]
                        ↓
                 [stream_write_next to client]
```

## Scope
v1: static file includes only (HTML fragments,SSI-style).
Dynamic includes (PHP) require async HTTP fetch — deferred to WordPress
LSCache plugin integration (v2).

## W3C ESI Include Syntax
```xml
<esi:include src="/path/to/fragment.html"/>
<esi:include src="/header.html" alt="/header-fallback.html"/>
```

- `src`: absolute path from docroot, or relative to current file
- `alt`: fallback if src not found (v1: read alt if src fails)
- No `onerror` attribute in v1

## Security
1. Path traversal: reject `..` in src
2. Symlink: use O_NOFOLLOW|O_CLOEXEC (consistent with dirwalker.c hardening)
3. File size: max 64KB per include (prevent memory exhaustion)
4. Nesting: max 3 levels deep (prevent infinite recursion)
5. Content-Type: only text/* MIME types (no binary injection)

## Implementation
In `esi_process()`, when state machine detects `<esi:include`:
1. Extract `src` attribute value
2. Resolve to absolute filesystem path (docroot + src)
3. Validate path (no .., exists, is regular file, size < 64KB)
4. Read file content
5. Replace the entire `<esi:include .../>` tag with file content
6. Continue processing (nested ESI tags in included content are processed)

## References
- W3C ESI Language Specification 1.0 §2.1
- LSWS EsiParserSM (reverse engineering, behavioral reference)
- Apache mod_include (SSI syntax similarity)


## v1.1 hardening (post codex-review)
- `src` resolves against the **session document root** carried in
  `esi_ctx_t` (was a hardcoded `/` — arbitrary file read, found by codex
  probe and self-review). Missing docroot refuses includes (fail closed).
- realpath containment: the resolved path must stay under the resolved
  docroot, closing intermediate-symlink escapes (e.g. an
  attacker-writable `uploads/` symlinked at `/etc`). `O_NOFOLLOW` pins
  the final component.
- Chunk-boundary reassembly: any tag not terminated by `>` in the
  current chunk is held (≤8KB) and re-presented next chunk; a partial
  `<esi:` prefix tail is likewise held. Oversized partials fail open
  (emitted literally). Stream end flushes held bytes literally.

## Wiring (v1.1)
- `EsiEnable On|Off` in .htaccess (default Off) gates the whole body
  filter; Content-Type must be text/html (+xhtml) and the body
  uncompressed (`esi_should_process`).
- `LSI_HKPT_RECV_RESP_BODY` runs the state machine per chunk and writes
  transformed output via `stream_write_next`; `LSI_HKPT_RCVD_RESP_BODY`
  flushes leftovers. Per-request state is a thread-local keyed on the
  session pointer + request sequence (same pattern as `req_cache`).
