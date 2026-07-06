ARG OLS_E2E_BASE_IMAGE=litespeedtech/openlitespeed:1.9.1-lsphp82
FROM ${OLS_E2E_BASE_IMAGE}

# Runtime dependency for the dynamically linked FilesMatch PCRE2 support.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpcre2-8-0 \
    && rm -rf /var/lib/apt/lists/*

# Copy the compiled litehttpd_htaccess module
COPY build/litehttpd_htaccess.so /usr/local/lsws/modules/litehttpd_htaccess.so

# Create test document root with static files for testing
RUN mkdir -p /var/www/vhosts/localhost/html/subdir && \
    echo "<h1>OK</h1>" > /var/www/vhosts/localhost/html/index.html && \
    echo "img" > /var/www/vhosts/localhost/html/photo.jpg && \
    echo "custom" > /var/www/vhosts/localhost/html/subdir/custom.html && \
    chown -R nobody:nogroup /var/www/vhosts/localhost

# Patch the default vhost config to enable .htaccess support
# The base image already has a working listener on 8088 and a default vhost
RUN CONF="/usr/local/lsws/conf/httpd_config.conf" && \
    sed -i 's|^[[:space:]]*vhRoot[[:space:]].*|    vhRoot                   /var/www/vhosts/localhost/|' "$CONF" || true; \
    for VHCONF in /usr/local/lsws/conf/vhosts/Example/vhconf.conf \
                  /usr/local/lsws/conf/vhosts/localhost/vhconf.conf; do \
      if [ -f "$VHCONF" ]; then \
        sed -i 's/^[[:space:]]*allowOverride.*/allowOverride             255/' "$VHCONF" || true; \
        grep -q 'allowOverride' "$VHCONF" || echo 'allowOverride 255' >> "$VHCONF"; \
        sed -i 's/^[[:space:]]*autoIndex[[:space:]].*/  autoIndex 1/' "$VHCONF" || true; \
      fi; \
    done

# Add module loading to httpd_config.conf if not already present
RUN CONF="/usr/local/lsws/conf/httpd_config.conf" && \
    if ! grep -q 'litehttpd_htaccess' "$CONF" 2>/dev/null; then \
      echo '' >> "$CONF"; \
      echo 'module litehttpd_htaccess {' >> "$CONF"; \
      echo '  ls_enabled              1' >> "$CONF"; \
      echo '}' >> "$CONF"; \
    fi

EXPOSE 8088
