%global ols_version  1.8.5
%global install_dir  /usr/local/lsws

Name:           openlitespeed-litehttpd
Version:        %{ols_version}
Release:        1.olsextra%{?dist}
Summary:        OpenLiteSpeed with PHPConfig + RewriteRule LSIAPI patches
AutoReqProv:    no

License:        GPLv3+
URL:            https://openlitespeed.org/
Source0:        https://github.com/litespeedtech/openlitespeed/archive/v%{ols_version}.tar.gz
Source1:        0001-lsiapi-phpconfig.patch
Source2:        0002-lsiapi-rewrite.patch

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.14
BuildRequires:  make
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  which
BuildRequires:  git
BuildRequires:  wget
BuildRequires:  golang
BuildRequires:  clang
BuildRequires:  patch
BuildRequires:  pcre-devel
BuildRequires:  openssl-devel
BuildRequires:  expat-devel
BuildRequires:  zlib-devel
BuildRequires:  bzip2-devel
BuildRequires:  libxml2-devel
BuildRequires:  curl-devel
BuildRequires:  libcap-devel
BuildRequires:  libaio-devel
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
# ── Patch build.sh to fix rpmbuild compatibility ──
#
# 1. Skip preparelibquic: lsquic submodule is already populated in the
#    tarball (git clone --recurse-submodules). The function would delete
#    and re-clone it, which breaks symlinks in some rpmbuild layouts.
sed -i 's/^preparelibquic$/# preparelibquic (skipped for rpmbuild)/' build.sh

# 2. Skip lsrecaptcha Go build: Go 1.21+ (EL9) dropped legacy GOPATH mode
#    that build.sh relies on. lsrecaptcha is a captcha helper, not needed.
sed -i '/^cd src\/modules\/lsrecaptcha$/,/^cd \.\.\/\.\.\/\.\.\/$/s/^/#/' build.sh

# 3. Disable cmake GIT_SUBMODULE: tarball is not a git repo, this avoids
#    cmake trying to run `git submodule update --force` during configure.
sed -i 's/-DMOD_LUA=\$MOD_LUA/-DMOD_LUA=$MOD_LUA -DGIT_SUBMODULE=OFF/' build.sh

bash build.sh
[ -e dist/bin/openlitespeed ] || exit 1

%install
# Follow the same pattern as official & OpenMandriva specs:
# patch ols.conf SERVERROOT -> buildroot, then run install.sh
sed -i "s#SERVERROOT=%{install_dir}#SERVERROOT=%{buildroot}%{install_dir}#" ols.conf
# Disable lsphp download — we don't bundle PHP
sed -i -e 's:USE_LSPHP7=yes:USE_LSPHP7=no:g' install.sh
bash install.sh

# Systemd unit
mkdir -p %{buildroot}%{_unitdir}
if [ -f %{buildroot}%{install_dir}/admin/misc/lshttpd.service ]; then
    mv %{buildroot}%{install_dir}/admin/misc/lshttpd.service %{buildroot}%{_unitdir}/
else
    sed "s:%%LSWS_CTRL%%:%{install_dir}/bin/lswsctrl:g" \
        dist/admin/misc/lshttpd.service.in > %{buildroot}%{_unitdir}/lshttpd.service
fi
chmod 644 %{buildroot}%{_unitdir}/lshttpd.service

# Init script (compat)
mkdir -p %{buildroot}/etc/init.d
if [ -f %{buildroot}%{install_dir}/admin/misc/lsws.rc ]; then
    cp %{buildroot}%{install_dir}/admin/misc/lsws.rc %{buildroot}/etc/init.d/lsws
else
    sed "s:%%LSWS_CTRL%%:%{install_dir}/bin/lswsctrl:" \
        dist/admin/misc/lsws.rc.in > %{buildroot}/etc/init.d/lsws 2>/dev/null || true
fi
chmod 0755 %{buildroot}/etc/init.d/lsws 2>/dev/null || true

# Mark as RPM install
echo 'RPM' > %{buildroot}%{install_dir}/PLAT

# Clean up buildroot prefix leaked into installed text files (must be LAST)
grep -rl "%{buildroot}" %{buildroot} 2>/dev/null | \
    xargs -r sed -i "s#%{buildroot}##g"
# Fix absolute symlinks that point into buildroot
find %{buildroot} -type l | while read link; do
    target=$(readlink "$link")
    case "$target" in *%{buildroot}*)
        newtarget="${target#%{buildroot}}"
        ln -snf "$newtarget" "$link"
    ;; esac
done

%pre
getent group lsadm > /dev/null || groupadd -r lsadm
getent group nogroup > /dev/null || groupadd -r nogroup
getent passwd lsadm > /dev/null || \
    useradd -g lsadm -d / -r -s /sbin/nologin -c "lsadm" lsadm >/dev/null 2>&1
exit 0

%post
%systemd_post lshttpd.service

%preun
if [ $1 = 0 ]; then
    %systemd_preun lshttpd.service
fi

%postun
%systemd_postun_with_restart lshttpd.service

%files
%defattr(-,root,root,-)
%license GPL.txt
%dir %{install_dir}
%{install_dir}/*
/etc/init.d/lsws
%{_unitdir}/lshttpd.service

%changelog
* Sat Apr 04 2026 LiteHTTPD <noreply@web.casa> - 1.8.5-1.olsextra
- OpenLiteSpeed v1.8.5 with LiteHTTPD patches
- Patch 0001: PHPConfig LSIAPI extensions
- Patch 0002: RewriteRule execution
