%global module_name  litehttpd_htaccess
%global install_dir  /usr/local/lsws

Name:           litehttpd
Version:        2.0.0
Release:        1%{?dist}
Summary:        Apache .htaccess compatibility module for OpenLiteSpeed

License:        GPLv3+
URL:            https://github.com/example/litehttpd
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.14
BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  git-core
BuildRequires:  libxcrypt-devel

Requires:       (openlitespeed >= 1.7.0 or openlitespeed-litehttpd >= 1.7.0)

%description
Apache .htaccess compatibility module for OpenLiteSpeed.
Supports 80 directive types including Header, Redirect, SetEnv, Expires,
php_value/php_flag, RewriteRule/RewriteCond/RewriteOptions/RewriteMap,
If/ElseIf/Else with ap_expr, AddDefaultCharset, DefaultType, Satisfy,
Options, Require (ip/env/all/valid-user), AuthType Basic, DirectoryIndex,
FilesMatch, RemoveType, RemoveHandler, Action, AllowOverride filtering,
and brute-force protection. Includes litehttpd-confconv Apache-to-OLS config converter.

Works with stock OpenLiteSpeed. For php_value/php_flag and RewriteRule
execution, use with openlitespeed-litehttpd (PHPConfig + Rewrite patches).

%prep
%autosetup -n %{name}-%{version}

%build
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{install_dir} \
    -DCMAKE_C_FLAGS="%{optflags}" \
    -DCMAKE_CXX_FLAGS="%{optflags}"
cmake --build build %{?_smp_mflags}

%check
ctest --test-dir build --output-on-failure

%install
install -D -m 755 build/%{module_name}.so \
    %{buildroot}%{install_dir}/modules/%{module_name}.so
install -D -m 755 build/litehttpd-confconv \
    %{buildroot}%{install_dir}/bin/litehttpd-confconv

%post
OLS_CONF="%{install_dir}/conf/httpd_config.conf"
if [ -f "$OLS_CONF" ] && ! grep -q '^module %{module_name}' "$OLS_CONF"; then
    cat >> "$OLS_CONF" <<'MODEOF'

module %{module_name} {
    ls_enabled              1
}
MODEOF
fi

%postun
if [ "$1" -eq 0 ]; then
    OLS_CONF="%{install_dir}/conf/httpd_config.conf"
    if [ -f "$OLS_CONF" ]; then
        sed -i '/^module %{module_name}\s*{/,/^}/d' "$OLS_CONF"
    fi
fi

%files
%license LICENSE
%{install_dir}/modules/%{module_name}.so
%{install_dir}/bin/litehttpd-confconv

%changelog
* Tue Mar 31 2026 litehttpd maintainer <noreply@example.com> - 2.0.0-1
- 80 directive types, 1017 tests
- If/ElseIf/Else conditional blocks with ap_expr engine
- RewriteOptions inherit/IgnoreInherit, RewriteMap txt/rnd/int
- IPv6 CIDR, Require env/ip, RemoveType, RemoveHandler, Action
- litehttpd-confconv Apache-to-OLS config converter (60+ directives)
- OLS Patch 0003: readApacheConf startup integration
- 4 rounds security audit + 7 Codex review rounds

* Sun Mar 30 2026 litehttpd maintainer <noreply@example.com> - 0.2.0-1
- 64 directive types, 734 unit tests
- PHPConfig dual-mode (native API + env-var fallback)
- URI_MAP hook (matches CyberPanel architecture)
- OLS native ls_hash/ls_shmhash integration via dlsym
