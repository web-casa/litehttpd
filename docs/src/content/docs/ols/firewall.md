---
title: Firewall Daemon
description: Kernel-level blocking — brute-force escalation to nftables via the litehttpd-firewalld daemon
---

When brute-force protection trips, LiteHTTPD can escalate the block from
application-level 403s to **kernel-level packet drops** through a small
companion daemon (`litehttpd-firewalld`, a single static Rust binary,
~450 KB, no runtime dependencies).

## How the chain works

```
client exceeds BruteForceAllowedAttempts
  → module sends "b,<ip>,<ttl>" over a unix socket   (exactly once per attacker per window)
    → litehttpd-firewalld adds the IP to an nftables set with a kernel timeout
      → packets from that IP are dropped at the INPUT and FORWARD hooks
        → TTL expires → kernel removes the rule, daemon sweeps its bookkeeping
```

The application-level 403 keeps working without the daemon — the daemon is
purely additive hardening (attackers are cut off before they can consume
even a connection slot).

## Install

```bash
# build (or download the release binary)
cd firewalld && cargo build --release
install -m 755 target/release/litehttpd-firewalld /usr/local/bin/

# systemd
install -m 644 firewalld/litehttpd-firewalld.service /etc/systemd/system/
systemctl daemon-reload && systemctl enable --now litehttpd-firewalld
```

The daemon must run as root (nftables). Its socket at
`/var/run/litehttpd/firewall.sock` is group-owned by `nobody`/`nogroup`
so the OLS worker can connect; override with `LITEHTTPD_FW_SOCK_GID`.

## Kernel layout

```
table inet litehttpd {
  set blocked4 { type ipv4_addr; flags timeout; }
  set blocked6 { type ipv6_addr; flags timeout; }
  chain input    { type filter hook input    priority -1; ip saddr @blocked4 drop; ip6 saddr @blocked6 drop; }
  chain blockfwd { type filter hook forward  priority -1; ip saddr @blocked4 drop; ip6 saddr @blocked6 drop; }
}
```

Two hook chains because DNAT'd traffic (container port maps, NAT gateways)
traverses FORWARD, not INPUT — the same dual approach fail2ban uses.
Per-block TTLs are kernel element timeouts, so rules age out even if the
daemon dies.

## Persistence

Blocks are persisted to `/var/lib/litehttpd/firewall.db` (debounced) and
re-applied on daemon start.

## Verified behaviour

Container CI (real OLS + nftables + WordPress `wp-login.php`):

```
attempt 1-3: 200        (within threshold)
attempt 4:   403        (threshold crossed, escalation fired once)
attempt 5:   connection dropped by the KERNEL
nft:         elements = { <ip> timeout 20s expires ... }
after TTL:   traffic served again (kernel timeout + daemon sweep)
```

## Environment

| Variable | Default | Purpose |
|----------|---------|---------|
| `LITEHTTPD_FW_SOCK` | `/var/run/litehttpd/firewall.sock` | Socket path (both sides honour it) |
| `LITEHTTPD_FW_DB` | `/var/lib/litehttpd/firewall.db` | Persistence file |
| `LITEHTTPD_FW_SOCK_GROUP` | `nobody` / `nogroup` | Group granted socket access |
| `LITEHTTPD_FW_SOCK_GID` | (from group lookup) | Numeric override |
