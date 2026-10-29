# Spec: litehttpd-firewalld — standalone firewall daemon

## Architecture
```
[OLS worker (module)]  --UDS text-->  [litehttpd-firewalld (Rust daemon)]
                                           |-- nftables (preferred)
                                           |-- iptables (fallback)
                                           `-- noop (degraded)
```

## Protocol (UDS, line-oriented text)
| Command | Response | Description |
|---------|----------|-------------|
| `b,<ip>[,ttl]` | `ok b <ip>` / `err ...` | Block IP, optional TTL seconds |
| `u,<ip>` | `ok u <ip>` / `err ...` | Unblock IP |
| `s` | `ok s N entries\n...` | List blocked IPs |
| `q` | `ok q` | Close connection |

## Backend detection (at startup)
1. `nft list tables` succeeds → nftables
2. `iptables -L` succeeds → iptables
3. Neither → noop (log warning, module-side degradation active)

## Persistence
- File: `/var/lib/litehttpd/firewall.db` (tab-separated: ip\ttimestamp\tttl)
- Saved on every mutation + every 30s
- Restored on startup; blocks re-applied

## Degradation (module side)
When daemon is unavailable:
- Module continues with per-worker SHM blocklist only (P4 infrastructure)
- Reconnect attempt every 30s
- No HTTP request is ever delayed by daemon unavailability

## Security
- Socket mode 0660 (OLS workers run as nobody)
- IP validation: inet_pton before forwarding to backend
- Backend commands use execve (no shell)
- Rate limit: max 1000 commands/second per connection

## References
- nftables set documentation
- systemd service unit conventions
- Design pattern: half-sync/half-async (POSA2) — though this daemon is simple
  enough that the UDS accept loop suffices
