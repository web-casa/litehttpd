# Spec: Response Body Access via LSI Stream Filter (P6)

## Key Finding
OLS 1.9.1 already has a complete stream-filter API for response body
transformation — the same mechanism modgzip uses. **No OLS patch is needed
for basic ESI processing.** This document describes the existing API.

## Mechanism (from OLS source, GPLv3)

### Hook Registration
```c
static lsi_serverhook_t server_hooks[] = {
    {LSI_HKPT_RECV_RESP_BODY, on_recv_body, priority, LSI_FLAG_ENABLED},
    {LSI_HKPT_RCVD_RESP_BODY, on_body_done, priority, LSI_FLAG_ENABLED},
    LSI_HOOK_END
};
```

### Data Flow (per body chunk)
```
[upstream/handler] → chunk → [filter chain: our module] → stream_write_next → [next filter / client]
```

### In the `RECV_RESP_BODY` hook callback:
- `rec->ptr1` / `rec->len1` — current body chunk (const char*, int)
- `stream_read_next(rec, buf, size)` — pull more data from upstream
- `stream_write_next(rec, buf, len)` — push transformed data downstream
- `stream_write_next(rec, NULL, 0)` — end of stream signal
- Return value: number of bytes consumed from `ptr1` (or LSI_ERROR)

### In the `RCVD_RESP_BODY` hook callback:
- Body is complete; cleanup module state
- No body data available in this hook

## Module State Management
```c
lsi_module_data_t: use g_api->set_module_data(session, module, LSI_DATA_HTTP, ptr)
g_api->get_module_data(session, module, LSI_DATA_HTTP)
```

## ESI Filter Design (P7)
Using the stream filter pattern:
1. Accumulate body chunks in a module-private buffer
2. Scan buffer for `<esi:` tag boundaries (state machine)
3. On complete tag: process (include = hold + subrequest, remove = strip,
   vars = substitute)
4. Flush non-ESI content downstream via `stream_write_next`
5. On `RCVD_RESP_BODY`: flush remaining buffer, cleanup

### Holding Back
ESI processing requires holding the body until tags are resolved. The
stream filter allows this: simply don't call `stream_write_next` until
ready. The upstream will pause when its buffer fills (back-pressure).

### Subrequest for <esi:include>
OLS internal subrequest: `g_api->send_local_req(...)` — need to verify
exact API in OLS source. If unavailable, P7 v1 uses `<esi:remove>` only
(content stripping) and `<esi:vars>` (variable substitution).

## References
- OLS source modgzip (stream filter example): src/modules/modgzip/modgzip.cpp
- OLS source include/ls.h (LSI API): stream_read_next/stream_write_next
- W3C ESI Language Specification 1.0

## Decision
**No OLS patch needed for P6.** The existing API is sufficient for
ESI v1 (remove/vars). For ESI v2 (include with subrequest), evaluate
send_local_req or a lightweight HTTP fetch to self.
