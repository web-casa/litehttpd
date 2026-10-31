---
title: 防火墙守护进程
description: 内核级封禁 —— 暴力破解升级到 nftables（litehttpd-firewalld 守护进程）
---

当暴力破解保护触发时，LiteHTTPD 可将封禁从应用层 403 升级为**内核级丢包**，
由一个小型伴随守护进程完成（`litehttpd-firewalld`，单个静态 Rust 二进制，
约 450 KB，无运行时依赖）。

## 链路原理

```
客户端超过 BruteForceAllowedAttempts
  → 模块经 unix socket 发送 "b,<ip>,<ttl>"   （每个攻击者每个窗口只发一次）
    → litehttpd-firewalld 将 IP 加入带内核超时的 nftables set
      → 该 IP 的数据包在 INPUT 与 FORWARD 钩子被丢弃
        → TTL 到期 → 内核移除规则，守护进程清理簿记
```

没有守护进程时应用层 403 照常工作——守护进程是纯增益加固（攻击者连
连接槽位都占不到）。

## 安装

```bash
# 构建（或下载 release 二进制）
cd firewalld && cargo build --release
install -m 755 target/release/litehttpd-firewalld /usr/local/bin/

# systemd
install -m 644 firewalld/litehttpd-firewalld.service /etc/systemd/system/
systemctl daemon-reload && systemctl enable --now litehttpd-firewalld
```

守护进程需要 root（nftables）。其 socket 位于
`/var/run/litehttpd/firewall.sock`，属组为 `nobody`/`nogroup` 以便 OLS
worker 连接；可用 `LITEHTTPD_FW_SOCK_GID` 覆盖。

## 内核布局

```
table inet litehttpd {
  set blocked4 { type ipv4_addr; flags timeout; }
  set blocked6 { type ipv6_addr; flags timeout; }
  chain input    { type filter hook input    priority -1; ip saddr @blocked4 drop; ip6 saddr @blocked6 drop; }
  chain blockfwd { type filter hook forward  priority -1; ip saddr @blocked4 drop; ip6 saddr @blocked6 drop; }
}
```

两条钩子链是因为 DNAT 流量（容器端口映射、NAT 网关）走 FORWARD 而非
INPUT——与 fail2ban 相同的双链做法。逐块 TTL 是内核 element 超时，
即使守护进程死亡规则也会自动过期。

## 持久化

封禁持久化到 `/var/lib/litehttpd/firewall.db`（防抖写入），守护进程
启动时重新应用。

## 已验证行为

容器 CI（真实 OLS + nftables + WordPress `wp-login.php`）：

```
第 1-3 次: 200        （阈值内）
第 4 次:   403        （越限，升级通知恰好发送一次）
第 5 次:   连接被内核丢弃
nft:       elements = { <ip> timeout 20s expires ... }
TTL 后:    流量恢复（内核超时 + 守护进程清理）
```

## 环境变量

| 变量 | 默认值 | 用途 |
|------|--------|------|
| `LITEHTTPD_FW_SOCK` | `/var/run/litehttpd/firewall.sock` | socket 路径（两侧一致） |
| `LITEHTTPD_FW_DB` | `/var/lib/litehttpd/firewall.db` | 持久化文件 |
| `LITEHTTPD_FW_SOCK_GROUP` | `nobody` / `nogroup` | 授予 socket 访问的组 |
| `LITEHTTPD_FW_SOCK_GID` | （按组名查找） | 数字覆盖 |
