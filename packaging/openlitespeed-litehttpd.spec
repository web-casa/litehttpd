%global ols_version  1.8.5
%global litehttpd_version 2.1.1
%global install_dir  /usr/local/lsws
%define debug_package %{nil}
# Disable all brp-* post-install processing to prevent binary replacement
%define __spec_install_post %{nil}
%define __os_install_post %{nil}

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
# Module source tarball (created by CI from the repo checkout)
Source9:        litehttpd-module-src.tar.gz

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
    # Pin to OLS version tag for reproducibility (falls back to HEAD if no tag)
    git clone --depth 1 --branch v%{ols_version} https://github.com/litespeedtech/third-party.git 2>/dev/null || \
    git clone --depth 1 https://github.com/litespeedtech/third-party.git
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

bash build.sh || true

# build.sh may report failure due to optional modules (pagespeed, lsrecaptcha)
# but the core binary is usually built. If not in dist/bin/, try direct make.
if [ ! -e dist/bin/openlitespeed ]; then
    echo "build.sh did not produce binary, running make directly..."
    make -C build -j$(nproc) openlitespeed 2>&1 | tail -5
    mkdir -p dist/bin dist/modules
    cp build/src/openlitespeed dist/bin/ 2>/dev/null || true
    # Copy any .so modules that were successfully built
    find build/src/modules -name "*.so" -exec cp {} dist/modules/ \; 2>/dev/null || true
fi
[ -e dist/bin/openlitespeed ] || exit 1

# Verify patches survived the build (CRITICAL: build.sh/install.sh can overwrite)
echo "=== Verifying patches in compiled binary ==="
PATCH_CHECK=$(strings dist/bin/openlitespeed | grep -c "set_php_config_value")
echo "set_php_config_value: $PATCH_CHECK occurrences"
if [ "$PATCH_CHECK" -lt 1 ]; then
    echo "WARNING: Patch 0001 (PHPConfig) not found in binary!"
    echo "Attempting to rebuild from cmake..."
    make -C build -j$(nproc) openlitespeed 2>&1 | tail -5
    cp build/src/openlitespeed dist/bin/openlitespeed
    PATCH_CHECK=$(strings dist/bin/openlitespeed | grep -c "set_php_config_value")
    echo "After rebuild: set_php_config_value: $PATCH_CHECK"
    [ "$PATCH_CHECK" -lt 1 ] && echo "FATAL: Patches still missing!" && exit 1
fi
# Save patched binary to build dir (not /tmp, avoids race on shared hosts)
SAVE_DIR=%{_builddir}/litehttpd-artifacts
mkdir -p $SAVE_DIR
cp dist/bin/openlitespeed $SAVE_DIR/openlitespeed-patched
echo "=== Saved patched binary ==="

# ── Build litehttpd_htaccess.so module in same EL8 environment ──
# Source9 is a tarball of the litehttpd repo (created by CI, no git clone needed)
echo "=== Building litehttpd module ==="
cd %{_builddir}
tar xzf %{SOURCE9} -C . 2>/dev/null || true
[ -d litehttpd-module/src ] || { echo "FATAL: litehttpd-module-src.tar.gz missing or invalid"; exit 1; }
cd litehttpd-module
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_C_FLAGS="-O2 -DNDEBUG" 2>&1 | tail -3
cmake --build build --target litehttpd_htaccess -j$(nproc) 2>&1 | tail -3
[ -f build/litehttpd_htaccess.so ] || { echo "FATAL: Module build failed!"; exit 1; }
strip build/litehttpd_htaccess.so
cp build/litehttpd_htaccess.so $SAVE_DIR/litehttpd_htaccess.so
echo "=== Module .so built: $(ls -lh $SAVE_DIR/litehttpd_htaccess.so | awk '{print $5}') ==="

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
# -I skips binary files (ELF, gzip, images, etc.) — sed on binaries corrupts them
grep -rlI "%{buildroot}" %{buildroot} 2>/dev/null | \
    xargs -r sed -i "s#%{buildroot}##g"
find %{buildroot} -type l | while read link; do
    target=$(readlink "$link")
    case "$target" in *%{buildroot}*)
        newtarget="${target#%{buildroot}}"
        ln -snf "$newtarget" "$link"
    ;; esac
done

# CRITICAL: Restore patched binary LAST — after all cleanup steps.
SAVE_DIR=%{_builddir}/litehttpd-artifacts
cp -f $SAVE_DIR/openlitespeed-patched %{buildroot}%{install_dir}/bin/openlitespeed
cp -f $SAVE_DIR/openlitespeed-patched dist/bin/openlitespeed 2>/dev/null || true
cp -f $SAVE_DIR/openlitespeed-patched build/src/openlitespeed 2>/dev/null || true
chmod 755 %{buildroot}%{install_dir}/bin/openlitespeed

# Install bundled module .so (built in same EL8 environment)
cp -f $SAVE_DIR/litehttpd_htaccess.so %{buildroot}%{install_dir}/modules/litehttpd_htaccess.so
chmod 755 %{buildroot}%{install_dir}/modules/litehttpd_htaccess.so
echo "=== Module .so installed into RPM ==="
P1=$(strings %{buildroot}%{install_dir}/bin/openlitespeed | grep -c "set_php_config_value")
P2=$(strings %{buildroot}%{install_dir}/bin/openlitespeed | grep -c "parse_rewrite_rules")
P3=$(strings %{buildroot}%{install_dir}/bin/openlitespeed | grep -c "readApacheConf")
echo "=== Final binary verification: P1=$P1 P2=$P2 P3=$P3 ==="
[ "$P1" -ge 1 ] && [ "$P2" -ge 1 ] && [ "$P3" -ge 1 ] || \
    { echo "FATAL: Patches missing!"; exit 1; }

%pre
getent group lsadm > /dev/null || groupadd -r lsadm
lsadm_gid=$(getent group lsadm 2>/dev/null | awk -F : '{ print $3; }')
getent passwd lsadm > /dev/null || \
    useradd -g ${lsadm_gid:-lsadm} -d / -r -s /sbin/nologin -c "lsadm" lsadm >/dev/null 2>&1
getent group nogroup > /dev/null || groupadd -r nogroup
exit 0

%post
%systemd_post lshttpd.service

# ── LiteHTTPD post-install tuning ──
# Strategy: use grep guards (idempotent) instead of $1==1.
# This ensures both fresh install AND upgrades get correct config,
# while preserving user customizations (grep won't match modified values).
CONF=%{install_dir}/conf/httpd_config.conf
VHCONF=%{install_dir}/conf/vhosts/Example/vhconf.conf

if [ -f "$CONF" ]; then
    # 1. Fix lsphp path: auto-detect installed lsphp version
    #    Supports: lsphp83, lsphp82, lsphp81, lsphp84, or user's custom path
    #    Only modifies if still pointing to old default fcgi-bin/lsphp
    LSPHP_BIN=""
    for ver in 84 83 82 81 80; do
        if [ -x "%{install_dir}/lsphp${ver}/bin/lsphp" ]; then
            LSPHP_BIN="\$SERVER_ROOT/lsphp${ver}/bin/lsphp"
            break
        fi
    done
    if [ -n "$LSPHP_BIN" ]; then
        sed -i "s|path.*fcgi-bin/lsphp5|path                            $LSPHP_BIN|" "$CONF" 2>/dev/null || true
        sed -i "s|path.*fcgi-bin/lsphp\b|path                            $LSPHP_BIN|" "$CONF" 2>/dev/null || true
    fi

    # 2. PHP worker pool: only add if not present (grep guard)
    if ! grep -q "PHP_LSAPI_CHILDREN" "$CONF" 2>/dev/null; then
        sed -i '/extProcessor lsphp/,/}/s/}$/  env                     PHP_LSAPI_CHILDREN=10\n  env                     LSAPI_AVOID_FORK=200M\n}/' "$CONF" 2>/dev/null || true
    fi

    # 3. Auto-enable litehttpd module: only if not already mentioned (grep guard)
    if [ -f "%{install_dir}/modules/litehttpd_htaccess.so" ]; then
        if ! grep -q "litehttpd_htaccess" "$CONF" 2>/dev/null; then
            printf '\nmodule litehttpd_htaccess {\n  ls_enabled 1\n}\n' >> "$CONF"
        fi
    fi
fi

if [ -f "$VHCONF" ]; then
    # 4. Enable rewrite: only inside "rewrite {" block, only if still "enable 0"
    #    (user who set enable 1 already won't be touched)
    sed -i '/^rewrite {/,/^}/s/enable 0/enable 1/' "$VHCONF" 2>/dev/null || true

    # 5. Add index.php to indexFiles: only if not already present
    if ! grep -q "index.php" "$VHCONF" 2>/dev/null; then
        sed -i 's/indexFiles index.html/indexFiles index.php, index.html/' "$VHCONF" 2>/dev/null || true
    fi
fi

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
