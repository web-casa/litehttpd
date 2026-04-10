---
title: "Installation Overview"
description: "Overview of OpenLiteSpeed installation methods, supported operating systems, and system requirements."
---

OpenLiteSpeed (OLS) is a lightweight, high-performance, open-source HTTP server. This section covers the available installation methods and requirements.

## Supported Operating Systems

| OS | Versions | Package Format |
|---|---|---|
| AlmaLinux / Rocky Linux | 9 | RPM |
| Ubuntu | 22.04 LTS, 24.04 LTS | DEB |
| Debian | 12 (Bookworm) | DEB |

Other RHEL 9-compatible distributions (Oracle Linux 9, CentOS Stream 9) are also supported via the RPM repository.

## Minimum System Requirements

| Resource | Minimum | Recommended |
|---|---|---|
| RAM | 1 GB | 2 GB+ |
| CPU | 1 core | 2+ cores |
| Disk | 500 MB free | 2 GB+ |
| Network | Public IP or localhost | Public IP with ports 80, 443, 7080 |

OLS is lightweight and can run on smaller instances than Apache or Nginx for equivalent workloads.

## Installation Methods

### Repository Install (Recommended)

The fastest path to a working OLS server. LiteSpeed provides official RPM and APT repositories with pre-built packages.

See [Install from Repository](/ols/install-repository/) for step-by-step instructions.

### Docker

Ideal for containerized environments, CI/CD pipelines, and quick evaluation. The official Docker image includes lsphp83 out of the box.

See [Docker Deployment](/ols/install-docker/) for details.

### Build from Source

For users who need custom compile-time options, patches, or non-standard platforms.

See [Build from Source](/ols/install-source/) for compilation instructions.

## Default Installation Layout

After installation, OLS files reside under `/usr/local/lsws/`:

```
/usr/local/lsws/
  bin/                  # Server binaries (lshttpd, lswsctrl)
  conf/                 # Configuration files
    httpd_config.conf   # Main server config
    vhosts/             # Virtual host configs
  admin/                # WebAdmin interface
  Example/              # Default virtual host docroot
  logs/                 # Server logs
  modules/              # Loadable modules
  tmp/                  # Temporary files
```

## Post-Install Checklist

Regardless of installation method, complete these steps after installing:

1. Set the WebAdmin password (see your installation method's page for the exact command).
2. Access the WebAdmin GUI at `https://your-server-ip:7080`.
3. Configure your first virtual host and listener.
4. Set up SSL/TLS certificates. See [SSL / TLS](/ols/ssl/).
5. Install PHP if needed. See [PHP Installation](/ols/php-install/).

## Next Steps

- [Install from Repository](/ols/install-repository/) -- the recommended path for most users.
- [Basic Configuration](/ols/basic-config/) -- understand the main config file structure.
- [Upgrade and Downgrade](/ols/upgrade/) -- keep your installation current.
