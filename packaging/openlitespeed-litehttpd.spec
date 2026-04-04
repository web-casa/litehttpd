%global ols_version  1.8.5
%global install_dir  /usr/local/lsws

Name:           openlitespeed-litehttpd
Version:        %{ols_version}
Release:        1.olsextra%{?dist}
Summary:        OpenLiteSpeed with PHPConfig + RewriteRule LSIAPI patches

License:        GPLv3+
URL:            https://openlitespeed.org/
Source0:        https://github.com/litespeedtech/openlitespeed/archive/v%{ols_version}.tar.gz
Source1:        0001-lsiapi-phpconfig.patch
Source2:        0002-lsiapi-rewrite.patch

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.14
BuildRequires:  make
BuildRequires:  pcre-devel
BuildRequires:  openssl-devel
BuildRequires:  expat-devel
BuildRequires:  zlib-devel
BuildRequires:  libxml2-devel
BuildRequires:  systemd-rpm-macros

Provides:       openlitespeed = %{ols_version}
Conflicts:      openlitespeed

Requires:       pcre
Requires:       openssl-libs
Requires:       expat
Requires:       zlib

%description
OpenLiteSpeed %{ols_version} with PHPConfig + RewriteRule LSIAPI patches applied.
Adds set_php_config_value/set_php_config_flag/get_php_config to lsi_api_t,
enabling litehttpd module to pass php_value/php_flag directives to lsphp
via the native PHPConfig::buildLsapiEnv() path.
Also adds parse_rewrite_rules/exec_rewrite_rules/free_rewrite_rules for
dynamic .htaccess RewriteRule execution.

%prep
%setup -q -n openlitespeed-%{ols_version}
patch -p1 < %{SOURCE1}
patch -p1 < %{SOURCE2}

%build
# OLS build.sh needs to detect package manager; provide a shim
# Third-party deps are already built by build.sh's internal logic
# but rpmbuild environment may not have them. Use cmake directly
# after build.sh prepares third-party libs.
if command -v dnf &>/dev/null; then
    export APP_MGR_CMD="dnf -y install"
elif command -v yum &>/dev/null; then
    export APP_MGR_CMD="yum -y install"
fi
# build.sh handles third-party (boringssl, brotli, etc.)
bash build.sh || {
    # Fallback: if build.sh fails on package detection,
    # try direct cmake build (deps already in rpmbuild)
    mkdir -p build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=%{install_dir} ..
    make %{?_smp_mflags}
}

%install
cd build
make install DESTDIR=%{buildroot}
mkdir -p %{buildroot}%{install_dir}/{conf,modules,logs,tmp}

# Systemd unit
install -D -m 644 dist/lsws.service \
    %{buildroot}%{_unitdir}/lsws.service 2>/dev/null || \
install -D -m 644 dist/lshttpd.service \
    %{buildroot}%{_unitdir}/lshttpd.service 2>/dev/null || true

%pre
getent group lsadm >/dev/null || groupadd -r lsadm
getent group nogroup >/dev/null || groupadd -r nogroup

%post
%systemd_post lsws.service lshttpd.service

%preun
%systemd_preun lsws.service lshttpd.service

%postun
%systemd_postun_with_restart lsws.service lshttpd.service

%files
%license LICENSE GPL.txt
%doc README.md
%{install_dir}/bin/
%{install_dir}/conf/
%{install_dir}/lib/
%{install_dir}/modules/
%{install_dir}/share/
%{install_dir}/admin/
%dir %{install_dir}/logs
%dir %{install_dir}/tmp
%{_unitdir}/*.service

%changelog
* Sat Apr 04 2026 LiteHTTPD <noreply@web.casa> - 1.8.5-1.litehttpd
- OpenLiteSpeed v1.8.5 with LiteHTTPD patches
- Patch 0001: PHPConfig LSIAPI extensions
- Patch 0002: RewriteRule execution
- Patch 0003: readApacheConf startup hook
- Patch 0004: Options -Indexes 403
