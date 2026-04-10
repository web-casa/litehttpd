---
title: "Build from Source"
description: "Compile OpenLiteSpeed from source with custom options."
---

Building from source is useful when you need custom compile-time flags, want to apply patches (such as the LiteHTTPD LSIAPI patches), or are targeting a non-standard platform.

## Prerequisites

Install the required build dependencies.

### AlmaLinux / Rocky Linux 9

```bash
dnf groupinstall "Development Tools"
dnf install gcc gcc-c++ cmake pcre-devel openssl-devel expat-devel zlib-devel
```

### Ubuntu / Debian

```bash
apt update
apt install build-essential gcc g++ cmake libpcre3-dev libssl-dev libexpat1-dev zlib1g-dev git
```

## Download and Build

```bash
git clone https://github.com/litespeedtech/openlitespeed.git
cd openlitespeed
bash build.sh
```

The `build.sh` script handles `cmake` configuration and compilation. It builds the server binary and all bundled modules.

## Install

After a successful build, install the server:

```bash
cd dist
bash install.sh
```

This installs OLS to `/usr/local/lsws/` with the same layout as a package install.

## Build Options

The `build.sh` script accepts several environment variables:

```bash
# Build with debug symbols
XTRABUILDFLAGS="-g -O0" bash build.sh

# Specify a custom OpenSSL path
CFLAGS="-I/opt/openssl/include" LDFLAGS="-L/opt/openssl/lib" bash build.sh
```

For more control, run cmake directly:

```bash
mkdir build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local/lsws
make -j$(nproc)
```

## Applying LiteHTTPD Patches

If you are using the LiteHTTPD module (`ols_htaccess.so`) with features that require custom OLS support (PHPConfig passthrough, LSIAPI Rewrite), apply the patches before building:

```bash
cd openlitespeed
git apply /path/to/patches/0001-lsiapi-phpconfig.patch
git apply /path/to/patches/0002-lsiapi-rewrite.patch
bash build.sh
cd dist && bash install.sh
```

See the [LiteHTTPD directive reference](/directives/) for which directives require the custom OLS build versus stock OLS.

## Post-Installation

After building and installing, follow the same post-install steps as a repository install:

```bash
# Set admin password
/usr/local/lsws/admin/misc/admpass.sh

# Start the server
/usr/local/lsws/bin/lswsctrl start

# Enable on boot (create systemd unit if needed)
```

The WebAdmin GUI is available at `https://your-server-ip:7080`.

## Next Steps

- [Install from Repository](/ols/install-repository/) -- if you prefer pre-built packages.
- [Upgrade and Downgrade](/ols/upgrade/) -- manage future version updates.
- [Basic Configuration](/ols/basic-config/) -- configure the server.
