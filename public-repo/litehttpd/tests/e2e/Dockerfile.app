FROM litespeedtech/openlitespeed:latest
# Requirements: 13.2, 13.3, 13.6

RUN apt-get update && apt-get install -y --no-install-recommends \
    lsphp81-mysql lsphp81-curl lsphp81-intl \
    curl unzip wget bzip2 mariadb-client \
    && rm -rf /var/lib/apt/lists/*

# Ensure CLI php uses lsphp81 (base image ships lsphp84 at /usr/bin/php).
RUN rm -f /usr/bin/php /usr/local/bin/php \
    && ln -s /usr/local/lsws/lsphp81/bin/php /usr/bin/php \
    && ln -s /usr/local/lsws/lsphp81/bin/php /usr/local/bin/php \
    && php -v

COPY build/litehttpd_htaccess.so /usr/local/lsws/modules/litehttpd_htaccess.so

RUN mkdir -p /var/www/vhosts/localhost/html/wordpress \
             /var/www/vhosts/localhost/html/nextcloud \
             /var/www/vhosts/localhost/html/drupal \
             /var/www/vhosts/localhost/html/laravel \
    && echo "<h1>App Stack OK</h1>" > /var/www/vhosts/localhost/html/index.html \
    && chown -R nobody:nogroup /var/www/vhosts/localhost

RUN CONF="/usr/local/lsws/conf/httpd_config.conf" && \
    if ! grep -q 'litehttpd_htaccess' "$CONF" 2>/dev/null; then \
      printf '\nmodule litehttpd_htaccess {\n  ls_enabled              1\n}\n' >> "$CONF"; \
    fi

# Enable .htaccess in vhost config (required for module to process directives)
RUN VHCONF="/usr/local/lsws/conf/vhosts/localhost/vhconf.conf" && \
    if [ -f "$VHCONF" ]; then \
      sed -i 's/^[[:space:]]*allowOverride.*/allowOverride             255/' "$VHCONF" || true; \
      grep -q 'allowOverride' "$VHCONF" || echo 'allowOverride 255' >> "$VHCONF"; \
    fi

RUN ln -sf /usr/local/lsws/lsphp81/bin/lsphp /usr/local/lsws/fcgi-bin/lsphp 2>/dev/null || true

RUN echo "=== httpd_config.conf ===" && \
    cat /usr/local/lsws/conf/httpd_config.conf && \
    echo "" && echo "=== fcgi-bin ===" && \
    ls -la /usr/local/lsws/fcgi-bin/ 2>/dev/null || true

EXPOSE 80 443
