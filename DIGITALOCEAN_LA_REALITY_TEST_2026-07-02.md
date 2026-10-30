# DigitalOcean West Coast Reality Test - 2026-07-02

## Scope

Validate the OpenLiteSpeed 1.9.1 LiteHTTPD release packaging on a real DigitalOcean VM, with explicit checks for the security regressions found during the 1.9.1 packaging review:

- no build-time WebAdmin `adminpasswd` in RPM or full tgz artifacts
- no shipped `phpbuild` source/build tree
- `admin/fcgi-bin/admin_php` is locally built PHP 7.4.33 with litespeed SAPI
- `admin_php` does not require `GLIBC_2.29+`
- patched OLS binary still contains the LiteHTTPD LSIAPI patch markers
- `litehttpd_htaccess.so` is present and loadable by the installed tree
- the service starts and answers HTTP requests on a fresh VM

## Region And Architecture Decision

DigitalOcean currently has no Los Angeles/LAX region in the API. The available US West Coast regions are:

- `sfo2` - San Francisco 2
- `sfo3` - San Francisco 3

DigitalOcean also exposes no ARM/Ampere/aarch64 Droplet size in the API or `doctl` size list. Because the reviewed P2 artifact is aarch64, direct aarch64 VM runtime testing is not possible on DigitalOcean. The real-world DO test will therefore use `sfo3` x86_64 with artifacts built through the same RPM spec/source-cache path. The aarch64 release artifact remains covered by local P2 builder/QEMU validation and package-content/ABI checks.

## Planned VM

- Provider: DigitalOcean
- Region: `sfo3`
- Image: AlmaLinux 8 x64
- Size: smallest available size that can install and run OLS comfortably, preferred `s-1vcpu-1gb` or `s-2vcpu-2gb`
- Name prefix: `litehttpd-ols191-reality-`
- Lifecycle: create, test, record results, destroy, confirm deletion

## Planned Artifact Inputs

Local reviewed aarch64 artifacts:

- `/tmp/ols-p2-artifacts/openlitespeed-litehttpd-1.9.1-4.litehttpd.el8.aarch64.rpm`
- `/tmp/ols-p2-artifacts/openlitespeed-litehttpd-1.9.1-aarch64-linux.tgz`

DO runtime test artifacts to build before VM creation:

- x86_64 RPM from `packaging/openlitespeed-litehttpd.spec` using `--with source_cache`
- x86_64 full tgz assembled from the cleaned RPM BUILDROOT

## Test Plan

1. Build or extract x86_64 artifacts from the same P2 release path.
2. Verify local artifact contents before upload:
   - RPM file list has no `/adminpasswd` or `/phpbuild`
   - full tgz file list has no `/adminpasswd` or `/phpbuild`
   - full tgz includes `bin/openlitespeed`, `modules/litehttpd_htaccess.so`, `admin/fcgi-bin/admin_php`, and `fcgi-bin/lsphp5`
3. Create a temporary `sfo3` Droplet.
4. Upload RPM and full tgz to `/root/litehttpd-test/`.
5. RPM path:
   - install runtime deps and the RPM
   - verify file ownership/content checks
   - verify `admin_php -v`
   - verify `readelf -V admin_php` has no `GLIBC_2.29+`
   - verify OLS starts through `systemctl` or `lswsctrl`
   - verify HTTP response from port 8088
   - verify patch markers in `bin/openlitespeed`
6. Full tgz path:
   - remove RPM install and clean `/usr/local/lsws`
   - extract full tgz and run its `install.sh`
   - repeat runtime/security checks
7. Record command outputs and conclusions in this report.
8. Destroy the Droplet and confirm it no longer exists.

## Results

Completed.

### Artifact Inputs Tested

aarch64 P2 artifacts retained for local/QEMU/package-content coverage:

- `openlitespeed-litehttpd-1.9.1-4.litehttpd.el8.aarch64.rpm`
  - SHA256: `6d8238e88911b8147d627b959d8ffc673f2f92631afae5c8c43a198cef0d0788`
- `openlitespeed-litehttpd-1.9.1-aarch64-linux.tgz`
  - SHA256: `fcb8ba88e479e2ff8f92ce11419438387e229b72a5bbd18eed88305706be62f4`

DigitalOcean x86_64 runtime-test artifacts built from the same P2 spec/source-cache path:

- `openlitespeed-litehttpd-1.9.1-4.litehttpd.el8.x86_64.rpm`
  - SHA256: `78116305c67beb80ea28287eaf3090f48e3ca848fe23410e921d063a1103582b`
- `openlitespeed-litehttpd-1.9.1-x86_64-linux.tgz`
  - SHA256: `73dfb739120ae6e473bfeea9d179613390bbc27d8d859e18068e08048d9fbb01`

Local artifact gates before upload:

- RPM file list: no `/adminpasswd`; no `/phpbuild`.
- Full tgz file list: no `adminpasswd`; no `phpbuild`.
- Full tgz includes:
  - `lsws/bin/openlitespeed`
  - `lsws/modules/litehttpd_htaccess.so`
  - `lsws/admin/fcgi-bin/admin_php`
  - `lsws/fcgi-bin/lsphp5`

### DigitalOcean VM

- Droplet id: `581701806`
- Name: `litehttpd-ols191-reality-20260702102732`
- Region: `sfo3`
- Image: `almalinux-8-x64`
- Size: `s-2vcpu-2gb`
- Test IP: `137.184.91.36`
- OS observed: AlmaLinux 8.10

### RPM Path

Result: PASS.

Checks completed:

- Installed `openlitespeed-litehttpd-1.9.1-4.litehttpd.el8.x86_64`.
- Verified installed tree has no `/usr/local/lsws/adminpasswd`.
- Verified installed tree has no `/usr/local/lsws/phpbuild`.
- Verified `admin/fcgi-bin/admin_php -v` reports `PHP 7.4.33 (litespeed)`.
- Verified `admin_php` does not require `GLIBC_2.29+`.
- Verified `fcgi-bin/lsphp5` runs.
- Verified patched OLS marker strings:
  - `set_php_config_value`
  - `parse_rewrite_rules`
  - `readApacheConf`
  - `indexes_disabled`
- Verified `/usr/local/lsws/modules/litehttpd_htaccess.so` exists and is an x86_64 ELF shared object.
- Started OLS through `systemctl enable --now lshttpd`.
- Verified HTTP response from `http://127.0.0.1:8088/`.

Note: The first RPM test script used `strings ... | grep -q` under `pipefail`, which returned 141 because `grep -q` closed the pipe early. The check was rerun using a temporary strings file and passed.

### Full TGZ Path

Result: PASS.

The first tgz run exposed a real installer issue on AlmaLinux 8 with SELinux
enforcing: the tgz installer used `cp -a`, which preserved the extracted
`admin_home_t` context on `/usr/local/lsws/bin/lswsctrl` and
`/usr/local/lsws/bin/openlitespeed`. `systemctl enable --now lshttpd` then
failed with systemd `203/EXEC`, although manual `lswsctrl start` succeeded.

Fix applied:

- `install-full.sh` now copies with `cp -a --no-preserve=context` when supported.
- The generated systemd unit now uses the OLS pidfile path `/tmp/lshttpd/lshttpd.pid`.
- `install-full.sh` runs `restorecon -RF "$PREFIX" /etc/systemd/system/lshttpd.service` when `restorecon` exists.
- x86_64 and aarch64 full tgz artifacts were reassembled after this fix, producing the updated SHA256 values above.

Retest VM:

- Droplet id: `581703272`
- Name: `litehttpd-ols191-tgz-retest-20260702103901`
- Region: `sfo3`
- Image: `almalinux-8-x64`
- Size: `s-2vcpu-2gb`
- Test IP: `146.190.147.86`
- OS observed: AlmaLinux 8.10 with SELinux enforcing

Checks completed:

- Cleaned `/usr/local/lsws`, `/etc/systemd/system/lshttpd.service`, and `/tmp/lshttpd`.
- Uploaded the reassembled `openlitespeed-litehttpd-1.9.1-x86_64-linux.tgz`.
- Extracted the full tgz and ran `./install.sh`.
- Verified installer contains the SELinux/systemd fix.
- Verified `/usr/local/lsws/bin/lswsctrl` and `/usr/local/lsws/bin/openlitespeed` have SELinux context `bin_t`.
- Verified `/etc/systemd/system/lshttpd.service` has SELinux context `systemd_unit_file_t`.
- Verified installed tree has no `/usr/local/lsws/adminpasswd`.
- Verified installed tree has no `/usr/local/lsws/phpbuild`.
- Verified `admin/fcgi-bin/admin_php -v` reports `PHP 7.4.33 (litespeed)`.
- Verified `admin_php` does not require `GLIBC_2.29+`.
- Verified `fcgi-bin/lsphp5` runs.
- Verified patched OLS marker strings:
  - `set_php_config_value`
  - `parse_rewrite_rules`
  - `readApacheConf`
  - `indexes_disabled`
- Verified `/usr/local/lsws/modules/litehttpd_htaccess.so` exists and is an x86_64 ELF shared object.
- Verified `systemctl enable --now lshttpd` succeeds.
- Verified `systemctl is-active --quiet lshttpd` succeeds.
- Verified HTTP response from `http://127.0.0.1:8088/`.

## Cleanup

Completed.

- Test droplet `581701806` destroyed and confirmed absent from the DigitalOcean API.
- Temporary DigitalOcean SSH key `57506477` deleted and confirmed absent from the DigitalOcean API.
- Retest droplet `581703272` destroyed and confirmed absent from the DigitalOcean API.
- Temporary DigitalOcean SSH key `57506601` deleted and confirmed absent from the DigitalOcean API.
- Confirmed there are no remaining droplets tagged `litehttpd-reality-test`.
