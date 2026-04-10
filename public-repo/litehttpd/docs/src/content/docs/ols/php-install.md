---
title: "Install PHP"
description: "Install PHP for OpenLiteSpeed using LiteSpeed repository packages"
---

## Overview

OpenLiteSpeed uses **lsphp** (LiteSpeed PHP) as its default PHP processor. lsphp is a PHP build optimized for LiteSpeed's native LSAPI protocol, delivering significantly better performance than PHP-FPM or standard FastCGI.

## Add the LiteSpeed Repository

### RHEL / AlmaLinux / Rocky Linux

```bash
rpm -Uvh https://rpms.litespeedtech.com/centos/litespeed-repo-1.3-1.el9.noarch.rpm
```

For EL8 systems, replace `el9` with `el8`.

### Debian / Ubuntu

```bash
wget -O - https://repo.litespeed.sh | sudo bash
```

## Install lsphp

### Available Versions

The LiteSpeed repository provides the following PHP versions:

| Package prefix | PHP version | Status |
|----------------|-------------|--------|
| `lsphp74` | 7.4.x | Security fixes only |
| `lsphp80` | 8.0.x | EOL |
| `lsphp81` | 8.1.x | Security fixes only |
| `lsphp82` | 8.2.x | Active support |
| `lsphp83` | 8.3.x | Active support |
| `lsphp84` | 8.4.x | Active support (recommended) |

### Install PHP 8.4 with Common Extensions

**RHEL / AlmaLinux / Rocky:**

```bash
dnf install lsphp84 lsphp84-mysqlnd lsphp84-xml lsphp84-gd \
  lsphp84-mbstring lsphp84-opcache lsphp84-curl lsphp84-json \
  lsphp84-zip lsphp84-bcmath lsphp84-intl
```

**Debian / Ubuntu:**

```bash
apt install lsphp84 lsphp84-mysql lsphp84-xml lsphp84-gd \
  lsphp84-mbstring lsphp84-opcache lsphp84-curl \
  lsphp84-zip lsphp84-bcmath lsphp84-intl
```

### List All Available Extensions

```bash
# RHEL-based
dnf list lsphp84-*

# Debian-based
apt-cache search lsphp84
```

## Link lsphp to OLS

OLS looks for the PHP binary at `/usr/local/lsws/fcgi-bin/lsphp`. Create a symlink to the installed version:

```bash
ln -sf /usr/local/lsws/lsphp84/bin/lsphp /usr/local/lsws/fcgi-bin/lsphp
```

To switch PHP versions later, update this symlink to point to a different lsphp binary (e.g., `lsphp83`).

## Verify the Installation

```bash
/usr/local/lsws/lsphp84/bin/lsphp -v
```

Expected output:

```
PHP 8.4.x (litespeed) ...
```

Note the `(litespeed)` SAPI identifier -- this confirms LSAPI support is compiled in.

## Multiple PHP Versions

You can install multiple lsphp versions simultaneously and assign them to different virtual hosts. Install each version:

```bash
dnf install lsphp83 lsphp84
```

Then configure separate external applications in `httpd_config.conf` for each version, pointing to the correct binary path:

```apacheconf
extprocessor lsphp83 {
  type                    lsapi
  address                 uds://tmp/lshttpd/lsphp83.sock
  maxConns                10
  env                     PHP_LSAPI_CHILDREN=10
  initTimeout             60
  retryTimeout            0
  respBuffer              0
  autoStart               1
  path                    /usr/local/lsws/lsphp83/bin/lsphp
  backlog                 100
  instances               1
}
```

## PHP Configuration

The `php.ini` for each lsphp version is located at:

```
/usr/local/lsws/lsphp84/etc/php/8.4/litespeed/php.ini
```

After modifying `php.ini`, restart OLS to apply changes:

```bash
systemctl restart lsws
```

## Next Steps

- [PHP LSAPI](/ols/php-lsapi/) -- understand how LSAPI works and tune it
- [PHP Environment Variables](/ols/php-env/) -- configure PHP worker processes
- [PHP-FPM Integration](/ols/php-fpm/) -- use PHP-FPM as an alternative
