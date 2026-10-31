use std::path::PathBuf;

fn db_path() -> PathBuf {
    std::env::var("LITEHTTPD_FW_DB")
        .unwrap_or_else(|_| "/var/lib/litehttpd/firewall.db".to_string())
        .into()
}

/// Save blocked IPs. Each entry: (ip, timestamp_secs, ttl_secs)
pub fn save(entries: &[(String, u64, Option<u64>)]) {
    let path = db_path();
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).ok();
    }
    let mut content = String::new();
    for (ip, ts, ttl) in entries {
        let ttl_str: String = match ttl {
            Some(t) => t.to_string(),
            None => String::new(),
        };
        content.push_str(&format!("{}\t{}\t{}\n", ip, ts, ttl_str));
    }
    let _ = std::fs::write(path, content);
}

/// Load blocked IPs. Returns Vec of (ip, timestamp_secs, ttl_secs)
pub fn load() -> Vec<(String, u64, Option<u64>)> {
    let path = db_path();
    let mut out = Vec::new();
    if let Ok(content) = std::fs::read_to_string(path) {
        for line in content.lines() {
            let parts: Vec<&str> = line.split('\t').collect();
            if parts.len() >= 2 {
                let ip = parts[0].to_string();
                let ts: u64 = parts[1].parse().unwrap_or(0);
                let ttl: Option<u64> = parts.get(2).and_then(|s| s.parse().ok());
                out.push((ip, ts, ttl));
            }
        }
    }
    out
}
