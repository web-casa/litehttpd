# Netcup ARM Runtime Test - 2026-07-02

## Target

- Host: 152.53.193.254
- Provider: netcup
- OS: AlmaLinux 10.2 (aarch64)
- Kernel: 6.12.0-211.28.1.el10_2.aarch64
- Tested artifact: `/tmp/ols-p2-artifacts/openlitespeed-litehttpd-1.9.1-aarch64-linux.tgz`
- Artifact SHA256 after installer refresh:
  `0dedf5d9b21d7ac566e4bf1ba73f8a34c7ced9a9af6b11fe98a78fc5c9280214`

## Initial Failure

The full tgz installed successfully, but the runtime binaries could not start
on AlmaLinux 10 because `libcrypt.so.1` was missing:

```text
openlitespeed: error while loading shared libraries: libcrypt.so.1: cannot open shared object file
admin_php: error while loading shared libraries: libcrypt.so.1: cannot open shared object file
lsphp5: error while loading shared libraries: libcrypt.so.1: cannot open shared object file
```

The RPM spec already covers this for EL10 with `Requires: libxcrypt-compat`.
The tgz installers did not.

## Fix

Updated:

- `packaging/tarball/install-full.sh`
- `packaging/tarball/install-patch.sh`

Both installers now check for `libcrypt.so.1` and install:

- EL/Fedora family: `libxcrypt-compat`
- Debian/Ubuntu family: `libcrypt1`

The aarch64 full and patch tgz artifacts in `/tmp/ols-p2-artifacts` were
repacked with the refreshed installers.

Patch tgz SHA256 after installer refresh:

```text
d3305d9cf1253aa5924dbc591a0d8f26023e08e1bacdc6bf9cec3af9c49e9989
```

## Retest Result

PASS.

Validated on the ARM VPS:

- Installer installed `libxcrypt-compat` and `libcrypt.so.1` became available.
- `openlitespeed` is an aarch64 ELF binary.
- `litehttpd_htaccess.so` is an aarch64 shared object.
- WebAdmin `admin_php` is locally built PHP 7.4.33 litespeed.
- `lsphp5` starts and reports PHP 7.4.33 litespeed.
- `admin_php` does not require `GLIBC_2.29+`.
- Patched symbols exist in `openlitespeed`:
  - `set_php_config_value`
  - `parse_rewrite_rules`
  - `readApacheConf`
  - `indexes_disabled`
- `adminpasswd` is not present.
- `phpbuild` is not present.
- `litehttpd_htaccess` module is enabled in `httpd_config.conf`.
- OpenLiteSpeed started successfully and listened on 8088/7080.
- Local HTTP GET to `127.0.0.1:8088` returned 13,733 bytes.

## Cleanup

- OpenLiteSpeed was stopped after the test.
- The temporary systemd unit was removed.
- `/usr/local/lsws` and `/root/litehttpd-arm-test` were left on the VPS for
  inspection; no `litespeed` process was running after cleanup.

## Remaining Release Blockers

- Full release artifact matrix is still incomplete locally:
  - missing all eight `.so` artifacts
  - missing EL9/EL10 full RPMs for both arches
  - missing EL9 standalone module RPMs for both arches
  - missing x86_64 patch/module tgz
  - missing aarch64 module tgz
- `bash scripts/quality.sh release-ready` currently fails at strict scope
  because the worktree still has out-of-scope dirty files.
