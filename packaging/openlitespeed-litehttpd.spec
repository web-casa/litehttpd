%global ols_version  1.8.5
%global install_dir  /usr/local/lsws

Name:           openlitespeed-litehttpd
Version:        %{ols_version}
Release:        2.litehttpd%{?dist}
Summary:        OpenLiteSpeed with LiteHTTPD patches (fully static, like official)
AutoReqProv:    no

License:        GPLv3+
URL:            https://openlitespeed.org/
Source0:        https://github.com/litespeedtech/openlitespeed/archive/v%{ols_version}.tar.gz
Source1:        0001-lsiapi-phpconfig.patch
Source2:        0002-lsiapi-rewrite.patch
Source3:        0003-readapacheconf.patch
Source4:        0004-autoindex-403.patch
Source5:        apacheconf_parser.c
Source6:        apacheconf_parser.h
Source7:        ols_config_writer.c
Source8:        ols_config_writer.h

# ── Build tools only — all libraries compiled from third-party sources ──
# Matches official LiteSpeed approach: NO system -devel packages.
# pcre, BoringSSL, expat, zlib, brotli, udns, etc. all built statically
# from litespeedtech/third-party repository.
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
BuildRequires:  clang
BuildRequires:  patch
BuildRequires:  perl
BuildRequires:  golang
BuildRequires:  systemd-rpm-macros

Provides:       openlitespeed = %{ols_version}
Conflicts:      openlitespeed

# Runtime deps — match official OLS RPM (no library deps, all static)
Requires:       /bin/sh
Requires:       libnsl
Requires:       openssl
%if 0%{?el10}
Requires:       libxcrypt-compat
%endif

%description
OpenLiteSpeed %{ols_version} with LiteHTTPD patches applied.
All third-party libraries (BoringSSL, pcre, expat, zlib, brotli, etc.)
are statically compiled from source, matching the official LiteSpeed build.

Patches:
- 0001: PHPConfig LSIAPI extensions (php_value/php_flag)
- 0002: RewriteRule execution (parse/exec/free)
- 0003: readApacheConf embedded parser (in-process Apache config conversion)
- 0004: Options -Indexes returns 403 (match Apache behavior)

%prep
%setup -q -n openlitespeed-%{ols_version}
# Copy embedded Apache config parser/writer into OLS source tree
cp %{SOURCE5} src/main/
cp %{SOURCE6} src/main/
cp %{SOURCE7} src/main/
cp %{SOURCE8} src/main/
# Apply patches
patch -p1 < %{SOURCE1}
patch -p1 < %{SOURCE2}
patch -p1 < %{SOURCE3}
patch -p1 < %{SOURCE4}

%build
# ── Build third-party static libraries (like official LiteSpeed) ──
# ── Build third-party static libraries (like official LiteSpeed) ──
# Clones litespeedtech/third-party and compiles pcre-8.45, BoringSSL,
# expat, zlib, brotli, udns, libmaxminddb, lmdb, curl, libxml2, yajl,
# libmodsecurity, bcrypt, ip2location as static libraries (.a).
# NOTE: Requires network access. CI runs in Docker with full network.
# For offline builds, pre-populate ../third-party/ before rpmbuild.
cd ..
if [ ! -d third-party ]; then
    git clone https://github.com/litespeedtech/third-party.git
    mkdir -p third-party/lib64
    cd third-party/script/
    # build_ols.sh has BUILD_LIBS list. The original list includes
    # "unittest-cpp" which no longer exists; upstream replaced it with
    # "bcrypt". This sed ensures compatibility with older third-party
    # snapshots that still reference unittest-cpp.
    sed -i -e "s/unittest-cpp/bcrypt/g" ./build_ols.sh
    ./build_ols.sh
    cd ../..
fi
cd openlitespeed-%{ols_version}

# ── Patch build.sh for rpmbuild compatibility ──

# 1. Skip preparelibquic: lsquic submodule already populated in tarball
sed -i 's/^\s*preparelibquic\s*$/# preparelibquic (skipped for rpmbuild)/' build.sh

# 2. Skip lsrecaptcha Go build (Go 1.21+ dropped legacy GOPATH mode)
sed -i '/cd src\/modules\/lsrecaptcha/,/cd \.\.\/.\.\/\.\.\//s/^/#/' build.sh
grep -q '^#.*lsrecaptcha' build.sh || { echo "FATAL: lsrecaptcha skip sed failed"; exit 1; }

# 3. Skip third-party clone in build.sh (already built above)
sed -i '/! -d third-party/,/^fi$/s/^/#/' build.sh
grep -q '^#.*third-party' build.sh || { echo "FATAL: third-party skip sed failed"; exit 1; }

# 4. Disable cmake GIT_SUBMODULE (tarball is not a git repo)
sed -i 's/-DMOD_LUA=\$MOD_LUA/-DMOD_LUA=$MOD_LUA -DGIT_SUBMODULE=OFF/' build.sh

bash build.sh
[ -e dist/bin/openlitespeed ] || exit 1

%install
sed -i "s#SERVERROOT=%{install_dir}#SERVERROOT=%{buildroot}%{install_dir}#" ols.conf
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

# Clean up buildroot prefix leaked into installed text files
grep -rl "%{buildroot}" %{buildroot} 2>/dev/null | \
    xargs -r sed -i "s#%{buildroot}##g"
find %{buildroot} -type l | while read link; do
    target=$(readlink "$link")
    case "$target" in *%{buildroot}*)
        newtarget="${target#%{buildroot}}"
        ln -snf "$newtarget" "$link"
    ;; esac
done

%pre
getent group lsadm > /dev/null || groupadd -r lsadm
lsadm_gid=$(getent group lsadm 2>/dev/null | awk -F : '{ print $3; }')
getent passwd lsadm > /dev/null || \
    useradd -g ${lsadm_gid:-lsadm} -d / -r -s /sbin/nologin -c "lsadm" lsadm >/dev/null 2>&1
getent group nogroup > /dev/null || groupadd -r nogroup
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
* Tue Apr 07 2026 LiteHTTPD <noreply@web.casa> - 1.8.5-2.litehttpd
- Switch to fully static build (match official LiteSpeed approach)
- All third-party libs built from source via litespeedtech/third-party
- No more system -devel package dependencies
- Binary now ~9MB (was 37MB with dynamic linking)
- EL10 fully supported (pcre1 statically compiled)
- Add libxcrypt-compat requirement for EL10

* Sat Apr 04 2026 LiteHTTPD <noreply@web.casa> - 1.8.5-1.litehttpd
- OpenLiteSpeed v1.8.5 with LiteHTTPD patches
- Patch 0001: PHPConfig LSIAPI extensions
- Patch 0002: RewriteRule execution
- Patch 0003: readApacheConf embedded parser
- Patch 0004: Options -Indexes returns 403
