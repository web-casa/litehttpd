mod backend;
mod persist;

use std::os::unix::net::{UnixListener, UnixStream};
use std::path::Path;
use std::sync::{Arc, Mutex};
use std::sync::atomic::{AtomicBool, Ordering};
use std::collections::HashMap;
use std::io::{Read, Write};
use std::net::IpAddr;

pub struct BlockEntry {
    pub ip: String,
    pub blocked_at: std::time::SystemTime,
    pub ttl: Option<u64>,
    pub expires_at: Option<std::time::SystemTime>,
}

pub struct State {
    pub blocked: HashMap<String, BlockEntry>,
    pub backend: Box<dyn backend::FirewallBackend + Send + Sync>,
    pub dirty: bool,
}

/// Validate that a string is a legitimate IP address (prevents command injection).
fn valid_ip(s: &str) -> bool {
    s.parse::<IpAddr>().is_ok()
}

/// Resolve the gid the socket should be group-owned by. Numeric
/// LITEHTTPD_FW_SOCK_GID wins; otherwise look up LITEHTTPD_FW_SOCK_GROUP
/// (default "nobody") in /etc/group. None = leave root-owned.
fn sock_group_gid() -> Option<u32> {
    if let Ok(g) = std::env::var("LITEHTTPD_FW_SOCK_GID") {
        return g.trim().parse::<u32>().ok();
    }
    let content = std::fs::read_to_string("/etc/group").ok()?;
    // "nobody" covers RHEL-family, "nogroup" Debian-family; an explicit
    // LITEHTTPD_FW_SOCK_GROUP skips both.
    let groups: Vec<String> = match std::env::var("LITEHTTPD_FW_SOCK_GROUP") {
        Ok(g) => vec![g],
        Err(_) => vec!["nobody".to_string(), "nogroup".to_string()],
    };
    for line in content.lines() {
        let cols: Vec<&str> = line.split(':').collect();
        if cols.len() >= 3 && groups.iter().any(|g| g == cols[0]) {
            return cols[2].parse::<u32>().ok();
        }
    }
    None
}

fn main() {
    let sock_path = std::env::var("LITEHTTPD_FW_SOCK")
        .unwrap_or_else(|_| "/var/run/litehttpd/firewall.sock".to_string());

    if let Some(dir) = Path::new(&sock_path).parent() {
        std::fs::create_dir_all(dir).ok();
    }
    std::fs::remove_file(&sock_path).ok();

    let listener = match UnixListener::bind(&sock_path) {
        Ok(l) => l,
        Err(e) => {
            eprintln!("fatal: cannot bind {}: {}", sock_path, e);
            std::process::exit(1);
        }
    };
    // The daemon must run as root (nftables), but the OLS worker (typically
    // user "nobody") needs to connect. 0660 alone leaves the socket
    // root:root, so every fw_client_block() from the worker gets EACCES
    // and escalation silently never happens. chgrp the socket to the
    // worker group: LITEHTTPD_FW_SOCK_GID (numeric) overrides the
    // default lookup of "nobody" in /etc/group.
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let _ = std::fs::set_permissions(&sock_path, std::fs::Permissions::from_mode(0o660));
        if let Some(gid) = sock_group_gid() {
            use std::os::unix::fs::chown;
            if let Err(e) = chown(&sock_path, None, Some(gid)) {
                eprintln!("warn: cannot chgrp socket to gid {}: {}", gid, e);
            }
        } else {
            eprintln!("warn: LITEHTTPD_FW_SOCK_GROUP not found; socket stays root-owned \
                      (set LITEHTTPD_FW_SOCK_GID if the web worker cannot connect)");
        }
    }

    let fw_backend = backend::detect();
    eprintln!("litehttpd-firewalld listening on {} (backend: {})", sock_path, fw_backend.name());

    let mut restored = HashMap::new();
    for (ip, ts, ttl) in persist::load() {
        if !valid_ip(&ip) { continue; }
        let blocked_at = std::time::UNIX_EPOCH + std::time::Duration::from_secs(ts);
        let expires_at = ttl.map(|t| blocked_at + std::time::Duration::from_secs(t));
        let _ = fw_backend.block(&ip);
        eprintln!("restored block: {}", ip);
        restored.insert(ip.clone(), BlockEntry { ip, blocked_at, ttl, expires_at });
    }

    let state = Arc::new(Mutex::new(State { blocked: restored, backend: fw_backend, dirty: false }));
    let shutdown = Arc::new(AtomicBool::new(false));

    // Debounced persistence + TTL cleanup (every 10s, saves only if dirty)
    {
        let st = state.clone();
        let sd = shutdown.clone();
        std::thread::spawn(move || {
            loop {
                std::thread::sleep(std::time::Duration::from_secs(10));
                if sd.load(Ordering::Relaxed) { break; }
                let mut guard = st.lock().unwrap();
                let now = std::time::SystemTime::now();
                let expired: Vec<String> = guard.blocked.iter()
                    .filter(|(_, e)| e.expires_at.map_or(false, |t| t < now))
                    .map(|(ip, _)| ip.clone())
                    .collect();
                for ip in expired {
                    guard.blocked.remove(&ip);
                    let _ = guard.backend.unblock(&ip);
                    eprintln!("expired: {}", ip);
                    guard.dirty = true;
                }
                if guard.dirty {
                    let entries: Vec<(String, u64, Option<u64>)> = guard.blocked.values()
                        .map(|e| (e.ip.clone(),
                            e.blocked_at.duration_since(std::time::UNIX_EPOCH)
                                .map(|d| d.as_secs()).unwrap_or(0),
                            e.ttl))
                        .collect();
                    persist::save(&entries);
                    guard.dirty = false;
                }
            }
        });
    }

    for stream in listener.incoming() {
        match stream {
            Ok(s) => {
                let st = state.clone();
                std::thread::spawn(move || handle_conn(s, st));
            }
            Err(e) => eprintln!("accept error: {}", e),
        }
    }
    shutdown.store(true, Ordering::Relaxed);
}

fn handle_conn(mut stream: UnixStream, state: Arc<Mutex<State>>) {
    let mut buf = [0u8; 256];
    loop {
        match stream.read(&mut buf) {
            Ok(0) => break,
            Ok(n) => {
                let cmd = String::from_utf8_lossy(&buf[..n]).to_string();
                for line in cmd.lines() {
                    let line = line.trim();
                    if line.is_empty() { continue; }
                    let response = process_command(line, &state);
                    let _ = stream.write_all(response.as_bytes());
                }
            }
            Err(_) => break,
        }
    }
}

fn process_command(cmd: &str, state: &Arc<Mutex<State>>) -> String {
    let parts: Vec<&str> = cmd.split(',').collect();
    let mut st = state.lock().unwrap();
    match parts[0] {
        "b" if parts.len() >= 2 => {
            let ip = parts[1].to_string();
            // IP validation: reject non-IP strings (command injection prevention)
            if !valid_ip(&ip) {
                return format!("err b {}: invalid IP address\n", ip);
            }
            let ttl: Option<u64> = parts.get(2).and_then(|s| s.parse().ok());
            let now = std::time::SystemTime::now();
            let expires = ttl.map(|t| now + std::time::Duration::from_secs(t));
            let result = st.backend.block(&ip);
            st.blocked.insert(ip.clone(), BlockEntry {
                ip: ip.clone(), blocked_at: now, ttl, expires_at: expires,
            });
            st.dirty = true; // debounced save, not immediate
            match result {
                Ok(_) => format!("ok b {}\n", ip),
                Err(e) => format!("err b {}: {}\n", ip, e),
            }
        }
        "u" if parts.len() >= 2 => {
            let ip = parts[1].to_string();
            if !valid_ip(&ip) {
                return format!("err u {}: invalid IP address\n", ip);
            }
            let result = st.backend.unblock(&ip);
            st.blocked.remove(&ip);
            st.dirty = true;
            match result {
                Ok(_) => format!("ok u {}\n", ip),
                Err(e) => format!("err u {}: {}\n", ip, e),
            }
        }
        "s" => {
            let mut lines = Vec::new();
            for e in st.blocked.values() {
                lines.push(format!("{} {:?} {:?}\n", e.ip, e.blocked_at, e.expires_at));
            }
            format!("ok s {} entries\n{}", st.blocked.len(), lines.join(""))
        }
        "q" => "ok q\n".to_string(),
        _ => format!("err unknown: {}\n", cmd),
    }
}
