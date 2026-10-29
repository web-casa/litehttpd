//! Firewall backend abstraction: nftables, iptables, or no-op.

use std::process::Command;

pub trait FirewallBackend: Send + Sync {
    fn block(&self, ip: &str) -> Result<(), String>;
    fn unblock(&self, ip: &str) -> Result<(), String>;
    fn name(&self) -> &str;
}

pub struct NftablesBackend;
pub struct IptablesBackend;
pub struct NoopBackend;

impl FirewallBackend for NftablesBackend {
    fn block(&self, ip: &str) -> Result<(), String> {
        let out = Command::new("nft")
            .args(["add", "element", "inet", "litehttpd", "blocked",
                   "{", ip, "timeout", "3600s", "}"])
            .output()
            .map_err(|e| e.to_string())?;
        if out.status.success() { Ok(()) }
        else { Err(String::from_utf8_lossy(&out.stderr).to_string()) }
    }
    fn unblock(&self, ip: &str) -> Result<(), String> {
        let out = Command::new("nft")
            .args(["delete", "element", "inet", "litehttpd", "blocked", "{", ip, "}"])
            .output()
            .map_err(|e| e.to_string())?;
        if out.status.success() { Ok(()) }
        else { Err(String::from_utf8_lossy(&out.stderr).to_string()) }
    }
    fn name(&self) -> &str { "nftables" }
}

impl FirewallBackend for IptablesBackend {
    fn block(&self, ip: &str) -> Result<(), String> {
        let out = Command::new("iptables")
            .args(["-I", "INPUT", "-s", ip, "-j", "DROP"])
            .output()
            .map_err(|e| e.to_string())?;
        if out.status.success() { Ok(()) }
        else { Err(String::from_utf8_lossy(&out.stderr).to_string()) }
    }
    fn unblock(&self, ip: &str) -> Result<(), String> {
        let out = Command::new("iptables")
            .args(["-D", "INPUT", "-s", ip, "-j", "DROP"])
            .output()
            .map_err(|e| e.to_string())?;
        if out.status.success() { Ok(()) }
        else { Err(String::from_utf8_lossy(&out.stderr).to_string()) }
    }
    fn name(&self) -> &str { "iptables" }
}

impl FirewallBackend for NoopBackend {
    fn block(&self, _ip: &str) -> Result<(), String> { Ok(()) }
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
