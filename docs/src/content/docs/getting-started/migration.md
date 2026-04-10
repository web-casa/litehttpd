---
title: Migration Guide
description: How to switch from stock OLS to LiteHTTPD across different installation methods
---

This guide covers switching from stock OpenLiteSpeed to LiteHTTPD across all common installation methods: manual/script install, CyberPanel, aaPanel, and Docker.

## General Concepts

LiteHTTPD comes in two editions:

- **Full mode** (`openlitespeed-litehttpd` RPM) — replaces the OLS binary with a patched version + installs `litehttpd_htaccess.so`. All 80 directives, including RewriteRule execution and php_value.
- **Thin mode** (module only) — copies `litehttpd_htaccess.so` to stock OLS. 70+ directives, but no RewriteRule execution or php_value passthrough.

Full mode is recommended for production use. Thin mode is a quick way to evaluate without touching the OLS binary.

---

## From Stock OLS (Script / Manual Install)

This applies to OLS installed via:
- [ols1clk.sh one-click script](https://docs.openlitespeed.org/installation/script/)
- Official LiteSpeed repository (`yum install openlitespeed` / `apt install openlitespeed`)
- Manual binary download

### Full Mode (Recommended)

```bash
# 1. Add LiteHTTPD repo and install (replaces stock OLS)
curl -s https://rpms.litehttpd.com/setup.sh | bash
dnf install openlitespeed-litehttpd

# 2. Restart
systemctl restart lsws
```

The RPM automatically:
- Replaces the OLS binary with the patched version (4 patches)
- Installs `litehttpd_htaccess.so` to `/usr/local/lsws/modules/`
- Adds the module block to `httpd_config.conf` (fresh install only)
- Enables rewrite in the Example vhost (fresh install only)

**Your existing config files are preserved** — the RPM uses `%config(noreplace)`, so `httpd_config.conf` and all vhost configs are not overwritten on upgrade.

### Important: Prevent Auto-Upgrade Reverting the Binary

If you originally installed OLS from the official LiteSpeed repo, a `dnf update` could pull in a newer `openlitespeed` package and overwrite the patched binary. Prevent this:

```bash
# Pin the package (EL 8/9/10)
dnf install python3-dnf-plugin-versionlock
dnf versionlock add openlitespeed

# Or exclude from updates
echo "exclude=openlitespeed" >> /etc/dnf/dnf.conf
```

Also disable the OLS built-in upgrade script:

```bash
# Rename lsup.sh to prevent in-place upgrades
mv /usr/local/lsws/admin/misc/lsup.sh /usr/local/lsws/admin/misc/lsup.sh.bak
```

### Thin Mode (Quick Evaluation)

If you prefer not to replace the OLS binary:

```bash
# Copy the module
cp litehttpd_htaccess.so /usr/local/lsws/modules/

# Enable in httpd_config.conf
cat >> /usr/local/lsws/conf/httpd_config.conf <<'EOF'

module litehttpd_htaccess {
    ls_enabled              1
}
EOF

# Restart
systemctl restart lsws
```

Thin mode limitations:
- `RewriteRule` / `RewriteCond` are parsed but **not executed** (no patch 0002)
- `php_value` / `php_flag` are parsed but **not passed to lsphp** (no patch 0001)
- `Options -Indexes` does not return 403 (no patch 0004)
- `readApacheConf` is not available (no patch 0003)

---

## From CyberPanel

CyberPanel installs OLS from the official repo, then replaces it with a **CyberPanel-patched binary** downloaded from `cyberpanel.net`. It also ships its own `.htaccess` module (`cyberpanel_ols.so`, ~29 directives, paid license).

### Key Differences

| Feature | CyberPanel .htaccess | LiteHTTPD |
|---------|---------------------|-----------|
| Directives | ~29 | 80 |
| RewriteRule execution | No | Yes (Full mode) |
| If/ElseIf/Else | No | Yes |
| ap_expr engine | No | Yes |
| Require directives | No | Yes |
| AuthType Basic | No | Yes |
| Options / AllowOverride | No | Yes |
| php_value to lsphp | Partial (PHP_INI_ALL only) | Full |
| License | $59/year or $199 lifetime | Free (GPLv3) |

### Migration Steps

**Option A: Full mode (replace CyberPanel binary)**

```bash
# 1. Disable CyberPanel's htaccess module first
sed -i '/module cyberpanel_ols/,/}/d' /usr/local/lsws/conf/httpd_config.conf

# 2. Install LiteHTTPD (replaces the CyberPanel-patched binary)
curl -s https://rpms.litehttpd.com/setup.sh | bash
dnf install openlitespeed-litehttpd

# 3. Restart
systemctl restart lsws
```

**Option B: Thin mode (keep CyberPanel binary)**

```bash
# 1. Disable CyberPanel's htaccess module
sed -i '/module cyberpanel_ols/,/}/d' /usr/local/lsws/conf/httpd_config.conf

# 2. Install LiteHTTPD module only
cp litehttpd_htaccess.so /usr/local/lsws/modules/
cat >> /usr/local/lsws/conf/httpd_config.conf <<'EOF'

module litehttpd_htaccess {
    ls_enabled              1
}
EOF

# 3. Restart
systemctl restart lsws
```

### CyberPanel-Specific Warnings

1. **CyberPanel upgrade overwrites the OLS binary.** Running `cyberpanel_upgrade` or clicking "Upgrade" in the CyberPanel UI calls `installCustomOLSBinaries()`, which downloads and replaces the binary from `cyberpanel.net`. After a CyberPanel upgrade, you must re-install LiteHTTPD:

   ```bash
   dnf reinstall openlitespeed-litehttpd
   systemctl restart lsws
   ```

2. **Do not load both modules simultaneously.** `cyberpanel_ols.so` and `litehttpd_htaccess.so` both handle `.htaccess` directives like `Header`, `ErrorDocument`, `php_value`. Loading both causes conflicts. Remove the `cyberpanel_ols` module block from `httpd_config.conf` before enabling LiteHTTPD.

3. **vhost.conf is auto-regenerated.** CyberPanel fully rewrites `/usr/local/lsws/conf/vhosts/{domain}/vhost.conf` on domain-level operations (SSL issuance, PHP version change, alias change). Do not add LiteHTTPD-specific config in vhost.conf — use `.htaccess` files instead, which CyberPanel does not touch.

4. **httpd_config.conf is safer.** CyberPanel modifies `httpd_config.conf` with surgical regex edits (not full regeneration), so the LiteHTTPD module block will survive panel operations.

5. **CyberPanel license check.** `cyberpanel_ols.so` validates its license every 24 hours against `platform.cyberpersons.com`. After removing the module, this check stops. No impact on LiteHTTPD.

---

## From aaPanel (BT Panel)

aaPanel installs OLS via its App Store as a one-click package. It uses standard OLS paths (`/usr/local/lsws/`) but manages config files through the panel UI.

### Migration Steps

```bash
# 1. Install LiteHTTPD
curl -s https://rpms.litehttpd.com/setup.sh | bash
dnf install openlitespeed-litehttpd

# 2. Restart
systemctl restart lsws
```

### aaPanel-Specific Warnings

1. **Do not upgrade OLS via aaPanel App Store.** The "Upgrade" button re-installs the stock OLS binary, overwriting the patched version. After any aaPanel-triggered OLS upgrade, re-install LiteHTTPD:

   ```bash
   dnf reinstall openlitespeed-litehttpd
   systemctl restart lsws
   ```

2. **vhost config regeneration.** aaPanel regenerates vhost `.conf` files when you change site settings (PHP version, SSL, domain aliases). Any custom directives added directly to `vhost.conf` may be lost. Use `.htaccess` files for per-site rules instead.

3. **httpd_config.conf may be modified.** aaPanel modifies `httpd_config.conf` on server-level changes. The LiteHTTPD module block is usually preserved, but verify after any aaPanel server configuration change:

   ```bash
   grep -c 'litehttpd_htaccess' /usr/local/lsws/conf/httpd_config.conf
   # Should output: 1
   ```

4. **Enable .htaccess loading.** Stock aaPanel OLS does not enable `.htaccess` auto-loading. After installing LiteHTTPD, ensure each vhost has:

   ```
   # In /usr/local/lsws/conf/vhosts/<name>/vhost.conf
   autoLoadHtaccess 1
   ```

   Or enable via OLS WebAdmin console (port 7080): Virtual Host > General > Auto Load .htaccess > Yes.

5. **PHP-FPM vs LSPHP.** Some aaPanel versions install PHP-FPM instead of LSPHP for OLS. LiteHTTPD's `php_value`/`php_flag` directives require LSPHP (LSAPI). If you see PHP-FPM in use, switch to LSPHP:

   ```bash
   dnf install lsphp83 lsphp83-common lsphp83-mysqlnd
   ```

   Then update the external app path in the vhost config to point to `/usr/local/lsws/lsphp83/bin/lsphp`.

---

## From ols-docker-env

The [ols-docker-env](https://github.com/litespeedtech/ols-docker-env) project uses `docker-compose` with the official `litespeedtech/openlitespeed` image. The OLS binary is baked into the image.

### Option A: Custom Docker Image (Recommended)

Build a custom image that includes the patched OLS binary and LiteHTTPD module:

```dockerfile
FROM litespeedtech/openlitespeed:1.8.5-lsphp83

# Install LiteHTTPD (Full mode)
COPY openlitespeed-patched /usr/local/lsws/bin/openlitespeed
COPY litehttpd_htaccess.so /usr/local/lsws/modules/
COPY litehttpd-confconv /usr/local/lsws/bin/

# Enable the module
RUN if ! grep -q 'litehttpd_htaccess' /usr/local/lsws/conf/httpd_config.conf; then \
    printf '\nmodule litehttpd_htaccess {\n    ls_enabled              1\n}\n' \
    >> /usr/local/lsws/conf/httpd_config.conf; \
    fi
```

Update `docker-compose.yml` to use your custom image:

```yaml
services:
  litespeed:
    image: your-registry/openlitespeed-litehttpd:1.8.5
    # ... rest of config unchanged
```

### Option B: Volume Mount (Quick Testing)

Mount the patched binary and module into the stock container:

```yaml
services:
  litespeed:
    image: litespeedtech/openlitespeed:1.8.5-lsphp83
    volumes:
      - ./lsws/conf:/usr/local/lsws/conf
      - ./sites:/var/www/vhosts
      # LiteHTTPD additions:
      - ./litehttpd/openlitespeed:/usr/local/lsws/bin/openlitespeed:ro
      - ./litehttpd/litehttpd_htaccess.so:/usr/local/lsws/modules/litehttpd_htaccess.so:ro
```

### Docker-Specific Warnings

1. **Container restart reverts changes.** Any in-container binary modification (e.g., running `lsup.sh` inside the container) is lost on restart. Use a custom image or volume mount.

2. **Config volumes are persistent.** The `./lsws/conf` directory is mounted from the host, so `httpd_config.conf` changes (including the module block) survive container restarts and image updates.

3. **Disable lsup.sh in the image.** The stock image includes `/usr/local/lsws/admin/misc/lsup.sh`. If using a custom image, remove or rename it to prevent accidental OLS upgrades via WebAdmin:

   ```dockerfile
   RUN mv /usr/local/lsws/admin/misc/lsup.sh /usr/local/lsws/admin/misc/lsup.sh.disabled
   ```

4. **Image updates.** When `litespeedtech/openlitespeed` releases a new image tag, do NOT blindly pull it — this would replace your custom image. Pin the image tag in `docker-compose.yml` and test new versions before updating.

---

## From OLS Official Docker Guide

The [official OLS Docker documentation](https://docs.openlitespeed.org/installation/docker/) uses the same `litespeedtech/openlitespeed` Docker image. The migration approach is identical to [ols-docker-env](#from-ols-docker-env) above.

The official docs recommend building a custom image with a `Dockerfile` — this is the best approach for integrating LiteHTTPD.

---

## Post-Migration Checklist

After switching to LiteHTTPD from any installation method:

1. **Verify the module is loaded:**

   ```bash
   grep 'litehttpd_htaccess' /usr/local/lsws/conf/httpd_config.conf
   ```

2. **Verify patches (Full mode only):**

   ```bash
   # Check key patch symbols are present in the binary
   strings /usr/local/lsws/bin/openlitespeed | grep -q 'set_php_config_value' && echo "patch 0001 OK"
   strings /usr/local/lsws/bin/openlitespeed | grep -q 'parse_rewrite_rules' && echo "patch 0002 OK"
   strings /usr/local/lsws/bin/openlitespeed | grep -q 'readApacheConf' && echo "patch 0003 OK"
   ```

3. **Test .htaccess processing:**

   ```bash
   # Create a test .htaccess
   echo 'Header set X-LiteHTTPD "active"' > /var/www/html/.htaccess

   # Check the response header
   curl -sI http://localhost/ | grep X-LiteHTTPD
   # Expected: X-LiteHTTPD: active
   ```

4. **Enable rewrite for WordPress:** See [Configuration — WordPress Permalinks](/getting-started/configuration/#wordpress-permalinks-return-404).

5. **Check for conflicting modules:** Ensure no other `.htaccess` module is loaded (e.g., `cyberpanel_ols.so`).

---

## Compatibility Notes

### What LiteHTTPD Does NOT Change

- OLS WebAdmin console (port 7080) — works normally
- OLS listener/vhost configuration — fully compatible
- LSCache / LiteSpeed Cache plugin — works normally
- LSAPI PHP handler — works normally (enhanced with php_value support in Full mode)
- SSL/TLS configuration — unchanged
- HTTP/3 / QUIC — unchanged

### What LiteHTTPD Changes

- `.htaccess` files are now fully processed (80 directives)
- The OLS binary has 4 additional patches (Full mode only)
- A new module (`litehttpd_htaccess.so`) is loaded
- `readApacheConf` directive is available in `httpd_config.conf` (Full mode, patch 0003)
