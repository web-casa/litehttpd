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
