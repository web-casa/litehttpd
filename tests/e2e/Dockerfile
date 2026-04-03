FROM litespeedtech/openlitespeed:latest

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
RUN VHCONF="/usr/local/lsws/conf/vhosts/localhost/vhconf.conf" && \
    if [ -f "$VHCONF" ]; then \
      sed -i 's/allowOverride.*/allowOverride             255/' "$VHCONF" || true; \
      grep -q 'allowOverride' "$VHCONF" || echo 'allowOverride 255' >> "$VHCONF"; \
    fi

# Add module loading to httpd_config.conf if not already present
RUN CONF="/usr/local/lsws/conf/httpd_config.conf" && \
    if ! grep -q 'litehttpd_htaccess' "$CONF" 2>/dev/null; then \
      echo '' >> "$CONF"; \
      echo 'module litehttpd_htaccess {' >> "$CONF"; \
      echo '  ls_enabled              1' >> "$CONF"; \
      echo '}' >> "$CONF"; \
    fi

EXPOSE 8088
