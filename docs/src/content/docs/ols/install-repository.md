---
title: "Install from Repository"
description: "Step-by-step instructions for installing OpenLiteSpeed from official RPM and APT repositories."
---

LiteSpeed maintains official package repositories for RPM-based and Debian-based distributions. This is the recommended installation method.

## AlmaLinux / Rocky Linux 9

### Add the Repository

```bash
rpm -Uvh https://rpms.litespeedtech.com/centos/litespeed-repo-1.3-1.el9.noarch.rpm
```

### Install OpenLiteSpeed

```bash
dnf install openlitespeed
```

### Install PHP (Optional)

```bash
dnf install lsphp83 lsphp83-common lsphp83-mysqlnd lsphp83-opcache
```

## Ubuntu 22.04 / 24.04

### Add the Repository and Install

```bash
wget -O - https://rpms.litespeedtech.com/debian/enable_lst_debian_repo.sh | bash
apt update
apt install openlitespeed
```

### Install PHP (Optional)

```bash
apt install lsphp83 lsphp83-common lsphp83-mysql lsphp83-opcache
```

## Debian 12

The same repository script works for Debian:

```bash
wget -O - https://rpms.litespeedtech.com/debian/enable_lst_debian_repo.sh | bash
apt update
apt install openlitespeed
```

## Post-Installation Setup

### Set WebAdmin Password

After installation, set the admin password for the WebAdmin GUI:

```bash
/usr/local/lsws/admin/misc/admpass.sh
```

This prompts for a username and password. The credentials are stored in `/usr/local/lsws/admin/conf/htpasswd`.

Alternatively, the randomly generated initial password can be found at:

```
/usr/local/lsws/adminpasswd
```

### Start and Stop the Server

```bash
# Start
/usr/local/lsws/bin/lswsctrl start

# Stop
/usr/local/lsws/bin/lswsctrl stop

# Restart
/usr/local/lsws/bin/lswsctrl restart

# Check status
/usr/local/lsws/bin/lswsctrl status
```

On systems using systemd:

```bash
systemctl start lsws
systemctl stop lsws
systemctl restart lsws
systemctl enable lsws   # Start on boot
```

### Access the WebAdmin GUI

Open your browser and navigate to:

```
https://your-server-ip:7080
```

Accept the self-signed certificate warning and log in with the credentials you set above. The WebAdmin interface lets you manage listeners, virtual hosts, SSL, and modules through a graphical interface.

### Verify the Installation

The default site is served on port 8088. Open:

```
http://your-server-ip:8088
```

You should see the OpenLiteSpeed congratulations page.

### Firewall Configuration

If you use `firewalld` (AlmaLinux/Rocky):

```bash
firewall-cmd --permanent --add-port=7080/tcp
firewall-cmd --permanent --add-port=80/tcp
firewall-cmd --permanent --add-port=443/tcp
firewall-cmd --reload
```

If you use `ufw` (Ubuntu/Debian):

```bash
ufw allow 7080/tcp
ufw allow 80/tcp
ufw allow 443/tcp
```

## Next Steps

- [Basic Configuration](/ols/basic-config/) -- understand the main config file.
- [Virtual Hosts](/ols/virtual-hosts/) -- set up your first site.
- [SSL / TLS](/ols/ssl/) -- enable HTTPS.
