ARG OLS_APP_BASE_IMAGE=litespeedtech/openlitespeed:1.9.1-lsphp82
FROM ${OLS_APP_BASE_IMAGE}
# Requirements: 13.2, 13.3, 13.6
ARG OLS_APP_LSPHP_VERSION=82

RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        ca-certificates curl unzip wget bzip2 libpcre2-8-0 mariadb-client python3; \
    required_suffixes="-curl -intl -mysql"; \
    optional_suffixes="-common -opcache"; \
    has_candidate() { \
        apt-cache policy "$1" | awk '/Candidate:/ { found=1; candidate=$2 } END { exit(found && candidate != "(none)" ? 0 : 1) }'; \
    }; \
    case "${OLS_APP_LSPHP_VERSION}" in auto|[0-9][0-9]) ;; *) echo "Unsupported OLS_APP_LSPHP_VERSION=${OLS_APP_LSPHP_VERSION}" >&2; exit 1 ;; esac; \
    php_pkg=""; \
    if [ "${OLS_APP_LSPHP_VERSION}" = auto ]; then \
        for candidate in $(apt-cache search --names-only '^lsphp[0-9]+$' | awk '{print $1}' | sort -Vr); do \
            missing=0; \
            for suffix in ${required_suffixes}; do \
                has_candidate "${candidate}${suffix}" || missing=1; \
            done; \
            if [ "${missing}" -eq 0 ]; then \
                php_pkg="${candidate}"; \
                break; \
            fi; \
        done; \
    else \
        php_pkg="lsphp${OLS_APP_LSPHP_VERSION}"; \
        has_candidate "${php_pkg}"; \
        for suffix in ${required_suffixes}; do \
            has_candidate "${php_pkg}${suffix}"; \
        done; \
    fi; \
    if [ -n "${php_pkg}" ]; then \
        packages="${php_pkg}"; \
        for suffix in ${required_suffixes} ${optional_suffixes}; do \
            package="${php_pkg}${suffix}"; \
            if has_candidate "${package}"; then \
                packages="${packages} ${package}"; \
            fi; \
        done; \
        apt-get install -y --no-install-recommends ${packages}; \
        php_bin="/usr/local/lsws/${php_pkg}/bin/php"; \
        lsphp_bin="/usr/local/lsws/${php_pkg}/bin/lsphp"; \
    else \
        php_bin="$(find /usr/local/lsws -path '*/bin/php' -type f | sort -Vr | head -n1)"; \
        lsphp_bin="$(find /usr/local/lsws -path '*/bin/lsphp' -type f | sort -Vr | head -n1)"; \
    fi; \
    test -x "${php_bin}"; \
    test -x "${lsphp_bin}"; \
    rm -f /usr/bin/php /usr/local/bin/php; \
    ln -s "${php_bin}" /usr/bin/php; \
    ln -s "${php_bin}" /usr/local/bin/php; \
    ln -sf "${lsphp_bin}" /usr/local/lsws/fcgi-bin/lsphp; \
    php -v; \
    for ext in curl intl mysqli pdo_mysql; do \
        php -m | grep -i -x "${ext}"; \
    done; \
    rm -rf /var/lib/apt/lists/*

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
RUN CONF="/usr/local/lsws/conf/httpd_config.conf" && \
    sed -i 's|^[[:space:]]*vhRoot[[:space:]].*|    vhRoot                   /var/www/vhosts/localhost/|' "$CONF" || true; \
    for VHCONF in /usr/local/lsws/conf/vhosts/Example/vhconf.conf \
                  /usr/local/lsws/conf/vhosts/localhost/vhconf.conf; do \
      if [ -f "$VHCONF" ]; then \
        sed -i 's/^[[:space:]]*allowOverride.*/allowOverride             255/' "$VHCONF" || true; \
        grep -q 'allowOverride' "$VHCONF" || echo 'allowOverride 255' >> "$VHCONF"; \
      fi; \
    done

RUN echo "=== httpd_config.conf ===" && \
    cat /usr/local/lsws/conf/httpd_config.conf && \
    echo "" && echo "=== fcgi-bin ===" && \
    ls -la /usr/local/lsws/fcgi-bin/ 2>/dev/null || true

EXPOSE 80 443
