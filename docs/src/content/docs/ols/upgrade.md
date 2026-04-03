---
title: "Upgrade and Downgrade"
description: "Procedures for upgrading and downgrading OpenLiteSpeed, including backup and rollback."
---

## Before You Upgrade

Always back up your configuration before upgrading.

### Back Up Configuration

```bash
# Create a timestamped backup
tar czf /root/ols-backup-$(date +%Y%m%d).tar.gz /usr/local/lsws/conf/
```

This preserves `httpd_config.conf`, all virtual host configs, SSL settings, and module configurations.

### Check Current Version

```bash
/usr/local/lsws/bin/lshttpd -v
```

## Upgrade via Repository

### AlmaLinux / Rocky Linux 9

```bash
dnf update openlitespeed
systemctl restart lsws
```

### Ubuntu / Debian

```bash
apt update
apt upgrade openlitespeed
systemctl restart lsws
```

The package manager preserves your existing configuration files. If a config file has been modified locally, you may be prompted to keep your version or install the package maintainer's version. Keep your version unless you know the new default is required.

## Upgrade via Source

If you built from source:

```bash
cd openlitespeed
git pull
bash build.sh
cd dist
bash install.sh
/usr/local/lsws/bin/lswsctrl restart
```

The install script detects an existing installation and preserves configuration files.

## Downgrade

### Repository Downgrade

On RPM-based systems, use `dnf downgrade`:

```bash
# List available versions
dnf --showduplicates list openlitespeed

# Downgrade to a specific version
dnf downgrade openlitespeed-1.7.19
systemctl restart lsws
```

On Debian-based systems:

```bash
# List available versions
apt-cache showpkg openlitespeed

# Install a specific version
apt install openlitespeed=1.7.19-1
systemctl restart lsws
```

### Source Downgrade

Check out the desired tag and rebuild:

```bash
cd openlitespeed
git checkout v1.7.19
bash build.sh
cd dist
bash install.sh
/usr/local/lsws/bin/lswsctrl restart
```

## Restore from Backup

If an upgrade causes problems, restore your configuration backup:

```bash
# Stop the server
systemctl stop lsws

# Restore config
tar xzf /root/ols-backup-YYYYMMDD.tar.gz -C /

# Restart
systemctl start lsws
```

## Verify After Upgrade

```bash
# Check version
/usr/local/lsws/bin/lshttpd -v

# Check server status
/usr/local/lsws/bin/lswsctrl status

# Review error log for startup issues
tail -50 /usr/local/lsws/logs/error.log
```

## Next Steps

- [Basic Configuration](/ols/basic-config/) -- review configuration after upgrading.
- [Logs](/ols/logs/) -- check logs for any post-upgrade issues.
