//! Firewall backend abstraction: nftables, iptables, or no-op.
//!
//! nftables layout (created idempotently on first use):
//!   table inet litehttpd {
//!     set blocked4 { type ipv4_addr; flags timeout; }
//!     set blocked6 { type ipv6_addr; flags timeout; }
//!     chain input { type filter hook input priority -1; policy accept;
//!                   ip  saddr @blocked4 drop
//!                   ip6 saddr @blocked6 drop }
//!   }
//! Element timeouts carry the per-block TTL, so the kernel ages rules out
//! even if this daemon dies (defense in depth alongside the daemon's own
//! expiry sweep).

use std::net::IpAddr;
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};

pub trait FirewallBackend: Send + Sync {
    fn block(&self, ip: &str, ttl_secs: Option<u64>) -> Result<(), String>;
    fn unblock(&self, ip: &str) -> Result<(), String>;
    fn name(&self) -> &str;
}

pub struct NftablesBackend;
pub struct IptablesBackend;
pub struct NoopBackend;

/// Run `nft` and treat "already exists" as success (idempotent setup).
fn nft_ok(args: &[&str]) -> Result<(), String> {
    let out = Command::new("nft").args(args).output().map_err(|e| e.to_string())?;
    if out.status.success() {
        return Ok(());
    }
    let err = String::from_utf8_lossy(&out.stderr);
    if err.contains("exists") || err.contains("File exists") {
        Ok(())
    } else {
        Err(err.to_string())
    }
}

fn ensure_nft_layout() -> Result<(), String> {
    static DONE: AtomicBool = AtomicBool::new(false);
    if DONE.load(Ordering::Relaxed) {
        return Ok(());
    }
    nft_ok(&["add", "table", "inet", "litehttpd"])?;
    nft_ok(&["add", "set", "inet", "litehttpd", "blocked4",
             "{", "type", "ipv4_addr;", "flags", "timeout;", "}"])?;
    nft_ok(&["add", "set", "inet", "litehttpd", "blocked6",
             "{", "type", "ipv6_addr;", "flags", "timeout;", "}"])?;
    // Two hook chains: traffic addressed to this host hits INPUT, while
    // DNAT'd traffic (container port maps, gateways, VMs behind NAT)
    // traverses FORWARD — block both so the rule bites in every
    // deployment shape (same approach fail2ban uses).
    nft_ok(&["add", "chain", "inet", "litehttpd", "input",
             "{", "type", "filter", "hook", "input", "priority", "-1;", "policy", "accept;", "}"])?;
    nft_ok(&["add", "rule", "inet", "litehttpd", "input", "ip", "saddr", "@blocked4", "drop"])?;
    nft_ok(&["add", "rule", "inet", "litehttpd", "input", "ip6", "saddr", "@blocked6", "drop"])?;
    nft_ok(&["add", "chain", "inet", "litehttpd", "blockfwd",
             "{", "type", "filter", "hook", "forward", "priority", "-1;", "policy", "accept;", "}"])?;
    nft_ok(&["add", "rule", "inet", "litehttpd", "blockfwd", "ip", "saddr", "@blocked4", "drop"])?;
    nft_ok(&["add", "rule", "inet", "litehttpd", "blockfwd", "ip6", "saddr", "@blocked6", "drop"])?;
    DONE.store(true, Ordering::Relaxed);
    Ok(())
}

/// Which named set an IP belongs in (v4/v6 are distinct nft types).
fn set_for(ip: &str) -> Result<&'static str, String> {
    match ip.parse::<IpAddr>() {
        Ok(IpAddr::V4(_)) => Ok("blocked4"),
        Ok(IpAddr::V6(_)) => Ok("blocked6"),
        Err(_) => Err(format!("invalid IP: {}", ip)),
    }
}

impl FirewallBackend for NftablesBackend {
    fn block(&self, ip: &str, ttl_secs: Option<u64>) -> Result<(), String> {
        ensure_nft_layout()?;
        let set = set_for(ip)?;
        // "timeout <n>s" element clause; a block without TTL is permanent
        // until explicitly unblocked. TTL 0 = no timeout requested.
        let ttl_str = match ttl_secs {
            Some(t) if t > 0 => format!(" timeout {}s", t),
            _ => String::new(),
        };
        let element = format!("{{ {}{} }}", ip, ttl_str);
        nft_ok(&["add", "element", "inet", "litehttpd", set, &element])
    }

    fn unblock(&self, ip: &str) -> Result<(), String> {
        let set = set_for(ip)?;
        let element = format!("{{ {} }}", ip);
        nft_ok(&["delete", "element", "inet", "litehttpd", set, &element])
    }

    fn name(&self) -> &str { "nftables" }
}

impl FirewallBackend for IptablesBackend {
    fn block(&self, ip: &str, ttl_secs: Option<u64>) -> Result<(), String> {
        // iptables has no native per-rule timeout; the daemon's expiry
        // sweep removes the rule when the block entry lapses.
        let _ = ttl_secs;
        let out = Command::new("iptables")
            .args(["-I", "INPUT", "-s", ip, "-j", "DROP"])
            .output().map_err(|e| e.to_string())?;
        if out.status.success() { Ok(()) }
        else { Err(String::from_utf8_lossy(&out.stderr).to_string()) }
    }

    fn unblock(&self, ip: &str) -> Result<(), String> {
        // Deleting an absent rule errors with "Bad rule / does not match";
        // an already-gone rule is success for our purposes.
        let out = Command::new("iptables")
            .args(["-D", "INPUT", "-s", ip, "-j", "DROP"])
            .output().map_err(|e| e.to_string())?;
        let err = String::from_utf8_lossy(&out.stderr);
        if out.status.success() || err.contains("Bad rule") || err.contains("matching") {
            Ok(())
        } else {
            Err(err.to_string())
        }
    }

    fn name(&self) -> &str { "iptables" }
}

impl FirewallBackend for NoopBackend {
    fn block(&self, _ip: &str, _ttl_secs: Option<u64>) -> Result<(), String> { Ok(()) }
    fn unblock(&self, _ip: &str) -> Result<(), String> { Ok(()) }
    fn name(&self) -> &str { "noop" }
}

pub fn detect() -> Box<dyn FirewallBackend + Send + Sync> {
    if Command::new("nft").args(["list", "tables"]).output()
        .map(|o| o.status.success()).unwrap_or(false) {
        Box::new(NftablesBackend)
    } else if Command::new("iptables").arg("-L").output()
        .map(|o| o.status.success()).unwrap_or(false) {
        Box::new(IptablesBackend)
    } else {
        eprintln!("warning: no firewall backend detected, using noop");
        Box::new(NoopBackend)
    }
}
