---
title: Docker Environments
description: Use LiteHTTPD with ols-docker-env and official OLS Docker images
---

## Overview

This page covers using LiteHTTPD with:
- [ols-docker-env](https://github.com/litespeedtech/ols-docker-env) (docker-compose based)
- [Official OLS Docker images](https://docs.openlitespeed.org/installation/docker/) (`litespeedtech/openlitespeed`)
- Custom Docker deployments

All approaches share the same base image: `litespeedtech/openlitespeed`. The key challenge is that the OLS binary is baked into the image and must be replaced or overlaid.

## Option A: Custom Docker Image (Recommended)

Build a custom image with the patched OLS binary and LiteHTTPD module:

```dockerfile
FROM litespeedtech/openlitespeed:1.8.5-lsphp83

# Copy patched binary and module
COPY openlitespeed-patched /usr/local/lsws/bin/openlitespeed
COPY litehttpd_htaccess.so /usr/local/lsws/modules/
COPY litehttpd-confconv /usr/local/lsws/bin/

# Enable the module (only if httpd_config.conf is not volume-mounted)
RUN if ! grep -q 'litehttpd_htaccess' /usr/local/lsws/conf/httpd_config.conf; then \
    printf '\nmodule litehttpd_htaccess {\n    ls_enabled              1\n}\n' \
    >> /usr/local/lsws/conf/httpd_config.conf; \
    fi

# Disable OLS built-in upgrade to prevent accidental binary overwrite
RUN mv /usr/local/lsws/admin/misc/lsup.sh /usr/local/lsws/admin/misc/lsup.sh.disabled 2>/dev/null || true
```

Update `docker-compose.yml`:

```yaml
services:
  litespeed:
    image: your-registry/openlitespeed-litehttpd:1.8.5
    # ... rest of config unchanged
```

## Option B: Volume Mount (Quick Testing)

Mount the patched binary and module into a stock container without building a custom image:

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

Add the module block to your host-side `./lsws/conf/httpd_config.conf`:

```
module litehttpd_htaccess {
    ls_enabled              1
}
```

## Option C: Thin Mode (No Binary Replacement)

If you only need Thin mode features (no RewriteRule execution, no php_value), you can mount just the module `.so` without replacing the binary:

```yaml
volumes:
  - ./litehttpd/litehttpd_htaccess.so:/usr/local/lsws/modules/litehttpd_htaccess.so:ro
```

## Docker-Specific Considerations

### Container Restart Reverts In-Container Changes

Any changes made inside a running container (e.g., running `lsup.sh`, editing files) are lost on restart. Always use volume mounts or custom images for persistent changes.

### Config Volumes Are Persistent

If `./lsws/conf` is mounted from the host (standard in ols-docker-env), your `httpd_config.conf` changes persist across container restarts and image updates.

### Pin the Image Tag

When `litespeedtech/openlitespeed` releases a new image, do not blindly pull it. Pin the tag in `docker-compose.yml` and test before upgrading:

```yaml
image: litespeedtech/openlitespeed:1.8.5-lsphp83  # pinned
```

### Disable lsup.sh

The stock image includes `/usr/local/lsws/admin/misc/lsup.sh`. Running it via the WebAdmin console downloads and replaces the OLS binary inside the container. In Option B (volume mount), the `:ro` flag prevents overwriting. In Option A, remove the script in the Dockerfile.

### Disable autoLoadHtaccess

If your mounted configs have OLS's `autoLoadHtaccess 1`, disable it to avoid double-processing:

```bash
sed -i 's/autoLoadHtaccess.*1/autoLoadHtaccess 0/' ./lsws/conf/vhosts/*/vhost.conf
```

## Verification

```bash
# Inside the container
docker exec -it litespeed bash

echo 'Header set X-LiteHTTPD "active"' > /var/www/vhosts/localhost/html/.htaccess
curl -sI http://localhost:8088/ | grep X-LiteHTTPD
# Expected: X-LiteHTTPD: active

# Verify patches (Full mode)
strings /usr/local/lsws/bin/openlitespeed | grep -q 'set_php_config_value' && echo "patch 0001 OK"
strings /usr/local/lsws/bin/openlitespeed | grep -q 'parse_rewrite_rules' && echo "patch 0002 OK"
strings /usr/local/lsws/bin/openlitespeed | grep -q 'readApacheConf' && echo "patch 0003 OK"
```
