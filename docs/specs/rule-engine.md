# Spec: Request body hooks + rule engine prototype (P9)

## Architecture
```
[client] → request → [URI_MAP hook (existing)] → ... → [RECV_REQ_BODY hook (new)]
                              ↓                              ↓
                        existing htaccess rules        body scan rules
                              ↓                              ↓
                        redirect/deny/etc            freq-limit / field-blocklist
                                                             ↓
                                                    P3 CIDR / P4 BotList / P5 firewall
```

## Hook Integration
Register `LSI_HKPT_RCVD_REQ_BODY` (body fully received) for scanning.
For streaming scan (large bodies), also register `LSI_HKPT_RECV_REQ_BODY`.

## Rule Types (v1 prototype)
1. **Rate limit**: `RateLimit N/M [burst=B] [action=deny|throttle]` per-IP in SHM
2. **Field blocklist**: `BodyDenyField name=pattern` — reject if request body
   field matches PCRE2 pattern (uses unified regex wrapper from P1)

## Directive Syntax (in .htaccess)
```
# Rate limiting (per-IP, shared across workers via P4 SHM)
RateLimit 100/60          # 100 requests per 60 seconds
RateLimit 1000/3600 burst=200 action=throttle

# Body field scanning (on RCVD_REQ_BODY)
BodyDenyField password="(root|admin|123456)"
BodyDenyField email="@banned-domain\.com$"
```

## Pipeline
Rate limit check at URI_MAP (before body read, cheap SHM lookup).
Body field scan at RCVD_REQ_BODY (after body received).

Violations:
- Rate limit exceed → increment BotList counter (P4) → threshold → firewall (P5)
- Field blocklist match → immediate 403 + increment BotList

## References
- Apache mod_ratelimit (behavioral reference)
- Apache mod_security SecRule (body scanning reference)
- W3C ESI vars for variable access pattern (consistent API)

## Implementation
New file: `src/htaccess_exec_ratelimit.c` + `src/htaccess_exec_body_scan.c`
Parser additions: RateLimit, BodyDenyField directives
SHM integration: reuse P4 brute_force infrastructure for rate counters
Firewall integration: reuse P5 UDS IPC for escalate-to-firewall
