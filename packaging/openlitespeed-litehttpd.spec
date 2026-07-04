%global ols_version  1.9.1
%global litehttpd_version 2.1.4
%global install_dir  /usr/local/lsws
%global third_party_commit 7297c534b844d808fce7ec42798ef2b1f3aa5259
%global php_version 7.4.33
%global lsapi_version 8.1
%global php_src_sha256 5a2337996f07c8a097e03d46263b5c98d2c8e355227756351421003bea8f463e
%global lsapi_src_sha256 aa79814b939a0877d83910e11d21255a801842f0fca0796fde970774f41c296e
%global psol_version 1.11.33.4
%global boringssl_commit 9fc1c33e9c21439ce5f87855a6591a9324e569fd
%global curl_tag curl-7_88_1
%global lmdb_commit 0c8b94738866b1ba7ffac27287fd6cab83f2ade4
%global modsec_version 3.0.15
%global modsec_commit 0fb4aff98b4980cf6426697d5605c424e3d5bb60
%global modsec_python_commit bc625d5bb0bac6a64bcce8dc9902208612399348
%global modsec_libinjection_commit 211782219663f889f471650150df12b623c5766e
%global modsec_mbedtls_commit 0fe989b6b514192783c469039edd325fd0989806
%global modsec_mbedtls_framework_commit dff9da04438d712f7647fd995bc90fadd0c0e2ce
%global modsec_tf_psa_crypto_commit 29160dd877d29658279fd683b2ae57b320ddcf09
%global modsec_mldsa_native_commit 5772b4f4a0105694b1203abb582273f78fa951b7
%global modsec_secrules_tests_commit a3d4405e5a2c90488c387e589c5534974575e35b
%bcond_with source_cache
%define debug_package %{nil}
# Disable all brp-* post-install processing to prevent binary replacement
%define __spec_install_post %{nil}
%define __os_install_post %{nil}

Name:           openlitespeed-litehttpd
Version:        %{ols_version}
Release:        4.litehttpd%{?dist}
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
%if %{with source_cache}
# Pinned source cache created by scripts/create-release-source-cache.sh.
Source10:       third-party-src.tar.gz
Source11:       php-%{php_version}.tar.gz
Source12:       php-litespeed-%{lsapi_version}.tgz
Source13:       third-party-deps-src.tar.gz
%endif

# ── Build tools only for OLS — all OLS libraries compiled from third-party sources ──
# Matches official LiteSpeed approach: NO system -devel packages for OLS itself.
# PCRE2, BoringSSL, expat, zlib, brotli, udns, etc. all built statically
# from litespeedtech/third-party repository. libcap/libaio/expat devel
# packages provide build-time system headers expected by upstream build.sh.
# libxml2-devel/zlib-devel are only for rebuilding WebAdmin admin_php locally
# so the full RPM/tgz does not ship upstream's aarch64 GLIBC_2.29 binary.
BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.14
BuildRequires:  make
BuildRequires:  bison
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  binutils
BuildRequires:  which
BuildRequires:  git
BuildRequires:  wget
BuildRequires:  clang
BuildRequires:  patch
BuildRequires:  perl
BuildRequires:  golang
BuildRequires:  libcap-devel
BuildRequires:  libaio-devel
BuildRequires:  expat-devel
BuildRequires:  libxml2-devel
BuildRequires:  zlib-devel
BuildRequires:  libstdc++-static
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
All third-party libraries (BoringSSL, PCRE2, expat, zlib, brotli, etc.)
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

# Source-level guard: a strings check can prove symbols exist, but not that the
# added control flow is correct. The rewrite patch must reject null/empty text,
# enforce the size limit, and then continue to parse valid rule text.
grep -q "HttpContext tmpCtx;" src/lsiapi/lsiapilib.cpp || \
    { echo "FATAL: rewrite patch missing parseRules context"; exit 1; }
grep -q "text_len > 1024 \\* 1024" src/lsiapi/lsiapilib.cpp || \
    { echo "FATAL: rewrite patch missing parse input limit"; exit 1; }
if awk '/if \(!rules_text \|\| text_len <= 0\)/ { getline n; if (n ~ /if \(text_len > 1024 \* 1024\)/) bad = 1 } END { exit bad ? 0 : 1 }' src/lsiapi/lsiapilib.cpp; then
    echo "FATAL: rewrite patch has broken guard and would always return NULL"
    exit 1
fi

%build
# ── Build third-party static libraries (like official LiteSpeed) ──
# Uses a pinned litespeedtech/third-party cache when CI passes
# --with source_cache. Manual builds can still fall back to fetching the same
# pinned commit online.
cd ..
%if %{with source_cache}
if [ ! -d third-party ]; then
    echo "=== Extracting pinned third-party source cache ==="
    rm -rf .third-party-cache
    mkdir -p .third-party-cache
    tar --no-same-owner -xzf %{SOURCE10} -C .third-party-cache
    if [ -d .third-party-cache/third-party/script ]; then
        mv .third-party-cache/third-party third-party
    elif [ -f .third-party-cache/script/build_ols.sh ]; then
        mv .third-party-cache third-party
    else
        echo "FATAL: third-party-src.tar.gz missing script/build_ols.sh"
        exit 1
    fi
    rm -rf .third-party-cache
    cache_commit=$(git -C third-party rev-parse HEAD 2>/dev/null || true)
    [ "$cache_commit" = "%{third_party_commit}" ] || \
        { echo "FATAL: third-party cache commit mismatch: $cache_commit"; exit 1; }
fi
echo "=== Extracting pinned third-party dependency source cache ==="
rm -rf .third-party-deps-cache third-party-deps-src
mkdir -p .third-party-deps-cache
tar xzf %{SOURCE13} -C .third-party-deps-cache
if [ -f .third-party-deps-cache/third-party-deps-src/SHA256SUMS ]; then
    mv .third-party-deps-cache/third-party-deps-src third-party-deps-src
elif [ -f .third-party-deps-cache/SHA256SUMS ]; then
    mv .third-party-deps-cache third-party-deps-src
else
    echo "FATAL: third-party-deps-src.tar.gz missing SHA256SUMS"
    exit 1
fi
rm -rf .third-party-deps-cache
(
    cd third-party-deps-src
    sha256sum -c SHA256SUMS
    grep -Fxq "ModSecurity %{modsec_commit}" MANIFEST
    grep -Fxq "bindings/python %{modsec_python_commit}" MANIFEST
    grep -Fxq "others/libinjection %{modsec_libinjection_commit}" MANIFEST
    grep -Fxq "others/mbedtls %{modsec_mbedtls_commit}" MANIFEST
    grep -Fxq "others/mbedtls/framework %{modsec_mbedtls_framework_commit}" MANIFEST
    grep -Fxq "others/mbedtls/tf-psa-crypto %{modsec_tf_psa_crypto_commit}" MANIFEST
    grep -Fxq "others/mbedtls/tf-psa-crypto/framework %{modsec_mbedtls_framework_commit}" MANIFEST
    grep -Fxq "others/mbedtls/tf-psa-crypto/drivers/pqcp/mldsa-native %{modsec_mldsa_native_commit}" MANIFEST
    grep -Fxq "test/test-cases/secrules-language-tests %{modsec_secrules_tests_commit}" MANIFEST
)
export LITEHTTPD_TP_DEPS_CACHE="%{_builddir}/third-party-deps-src"
%endif
if [ ! -d third-party ]; then
    echo "=== Fetching pinned third-party source online ==="
    git init third-party
    git -C third-party remote add origin https://github.com/litespeedtech/third-party.git
    git -C third-party fetch --depth 1 origin %{third_party_commit}
    git -C third-party checkout --detach %{third_party_commit}
fi
[ -f third-party/script/build_ols.sh ] || \
    { echo "FATAL: third-party checkout missing script/build_ols.sh"; exit 1; }
mkdir -p third-party/lib64
cd third-party/script/
    # build_ols.sh has BUILD_LIBS list. The original list includes
    # "unittest-cpp" which no longer exists; upstream replaced it with
    # "bcrypt". This sed ensures compatibility with older third-party
    # snapshots that still reference unittest-cpp.
    sed -i -e "s/unittest-cpp/bcrypt/g" ./build_ols.sh
    # Shallow submodule checkouts leave several upstream build scripts on a
    # detached commit. Their unconditional "git pull" can then fail with
    # divergent-branch errors before they check out their pinned tag. Fetching
    # tags is enough for those scripts and does not mutate the checkout.
    sed -i -e 's/^[[:space:]]*git pull[[:space:]]*$/git fetch --tags --force || true/' ./build_*.sh
    # build_expat.sh always clones libexpat, but libexpat is already present
    # when build_ols.sh initializes third-party submodules. Reuse the pinned
    # submodule checkout and fetch tags so its later checkout works.
    sed -i -e 's#^git clone https://github.com/libexpat/libexpat.git$#[ -d libexpat/.git ] || [ -f libexpat/.git ] || git clone https://github.com/libexpat/libexpat.git#' \
        -e '/^cd libexpat$/a git fetch --tags --force || true' ./build_expat.sh
    # Fresh udns checkouts do not have a Makefile until configure runs.
    sed -i -e 's/^[[:space:]]*make clean[[:space:]]*$/make clean || true/' ./build_udns.sh
    # build_lmdb declares the GitHub LMDB mirror as the desired remote but
    # clones the full OpenLDAP repository and then checks out a non-existent
    # master branch. Use the mirror's mdb.master branch shallowly.
    sed -i \
        -e 's#git clone https://git.openldap.org/openldap/openldap.git lmdb#git clone --depth 1 --branch mdb.master https://github.com/LMDB/lmdb.git lmdb#' \
        -e 's#git checkout master#git checkout --force mdb.master#' \
        ./build_lmdb.sh

    cat > ./build_zlib.sh <<'BUILD_ZLIB_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
mkdir -p "$PREFIX/src"
cd "$PREFIX/src"

if [ -n "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    rm -rf zlib
    tar xzf "$LITEHTTPD_TP_DEPS_CACHE/zlib-e3dc0a85b7032e98380dec011bc8f2c2ee0d8fca.tar.gz"
    mv zlib-e3dc0a85b7032e98380dec011bc8f2c2ee0d8fca zlib
elif [ ! -d "zlib" ]; then
    git clone https://github.com/madler/zlib.git
fi

cd zlib
CFLAGS="-fPIC -O3" ./configure --prefix="$PREFIX" --static
make install
BUILD_ZLIB_SH
    chmod +x ./build_zlib.sh

    cat > ./build_bcrypt.sh <<'BUILD_BCRYPT_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
mkdir -p "$PREFIX/src"
cd "$PREFIX/src"

if [ -n "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    rm -rf libbcrypt
    tar xzf "$LITEHTTPD_TP_DEPS_CACHE/libbcrypt-55ff64349dec3012cfbbb1c4f92d4dbd46920213.tar.gz"
    mv libbcrypt-55ff64349dec3012cfbbb1c4f92d4dbd46920213 libbcrypt
elif [ ! -d "libbcrypt" ]; then
    git clone https://github.com/litespeedtech/libbcrypt.git
fi

cd libbcrypt
make
cp bcrypt.h ../../include
cp bcrypt.a ../../lib/libbcrypt.a
BUILD_BCRYPT_SH
    chmod +x ./build_bcrypt.sh

    cat > ./build_libaio.sh <<'BUILD_LIBAIO_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
mkdir -p "$PREFIX/src"
cd "$PREFIX/src"

PACKAGE=libaio-0.3.113
rm -rf "$PACKAGE" libaio-libaio-0.3.113
if [ -n "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    cp "$LITEHTTPD_TP_DEPS_CACHE/$PACKAGE.tar.gz" "$PACKAGE.tar.gz"
else
    wget --no-check-certificate https://pagure.io/libaio/archive/libaio-0.3.113/libaio-libaio-0.3.113.tar.gz
    mv libaio-libaio-0.3.113.tar.gz "$PACKAGE.tar.gz"
fi
tar xzf "$PACKAGE.tar.gz"
mv libaio-libaio-0.3.113 "$PACKAGE"
rm -f "$PACKAGE.tar.gz"

cd "$PACKAGE"
make prefix="$PREFIX" install
rm -f "$PREFIX"/lib/libaio.so*
BUILD_LIBAIO_SH
    chmod +x ./build_libaio.sh

    cat > ./build_pcre.sh <<'BUILD_PCRE_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
mkdir -p "$PREFIX/src"
cd "$PREFIX/src"

PACKAGE=pcre-8.45
rm -rf "$PACKAGE"
if [ -n "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    cp "$LITEHTTPD_TP_DEPS_CACHE/$PACKAGE.tar.gz" "$PACKAGE.tar.gz"
else
    wget "https://ftp.exim.org/pub/pcre/$PACKAGE.tar.gz"
fi
tar xzf "$PACKAGE.tar.gz"
rm -f "$PACKAGE.tar.gz"

cd "$PACKAGE"
./configure --prefix="$PREFIX" --enable-jit --enable-unicode-properties --enable-utf --with-pic --with-match-limit=100000 --enable-shared=no
make install
BUILD_PCRE_SH
    chmod +x ./build_pcre.sh

    cat > ./build_pcre2.sh <<'BUILD_PCRE2_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
mkdir -p "$PREFIX/src"
cd "$PREFIX/src"

PACKAGE=pcre2-10.45
rm -rf "$PACKAGE"
if [ -n "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    cp "$LITEHTTPD_TP_DEPS_CACHE/$PACKAGE.tar.gz" "$PACKAGE.tar.gz"
else
    wget "https://github.com/PCRE2Project/pcre2/releases/download/$PACKAGE/$PACKAGE.tar.gz"
fi
tar xzf "$PACKAGE.tar.gz"
rm -f "$PACKAGE.tar.gz"

cd "$PACKAGE"
./configure --prefix="$PREFIX" --enable-jit --enable-pcre2-8 --enable-unicode --with-pic --with-match-limit=100000 --enable-shared=no --enable-static
make install
BUILD_PCRE2_SH
    chmod +x ./build_pcre2.sh

    cat > ./build_psol.sh <<'BUILD_PSOL_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
mkdir -p "$PREFIX/src"
cd "$PREFIX/src"

PSOL_VER=1.11.33.4
TARGET="$PSOL_VER.tar.gz"

if [ ! -f "psol-$PSOL_VER/include/out/Release/obj/gen/net/instaweb/public/version.h" ]; then
    rm -rf psol "psol-$PSOL_VER" "../psol-$PSOL_VER"
    if [ -n "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
        cp "$LITEHTTPD_TP_DEPS_CACHE/$TARGET" "$TARGET"
    else
        curl -O -k "https://dl.google.com/dl/page-speed/psol/$TARGET"
    fi
    tar -xzf "$TARGET"
    mv psol "psol-$PSOL_VER"
    cd ..
    ln -sf "src/psol-$PSOL_VER" "psol-$PSOL_VER"

    cat <<'EOF' > "psol-$PSOL_VER/include/pagespeed/kernel/base/scoped_ptr.h"
/**
* Due the compiling issue, this file was updated from the original file.
*/
#ifndef PAGESPEED_KERNEL_BASE_SCOPED_PTR_H_
#define PAGESPEED_KERNEL_BASE_SCOPED_PTR_H_
#include "base/memory/scoped_ptr.h"

namespace net_instaweb {
template<typename T> class scoped_array : public scoped_ptr<T[]> {
public:
    scoped_array() : scoped_ptr<T[]>() {}
    explicit scoped_array(T* t) : scoped_ptr<T[]>(t) {}
};
}
#endif
EOF
fi
BUILD_PSOL_SH
    chmod +x ./build_psol.sh

    cat > ./build_libxml2.sh <<'BUILD_LIBXML2_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
mkdir -p "$PREFIX/src"
cd "$PREFIX/src"

VERSION=2.15.3
if [ ! -e "libxml2-$VERSION" ]; then
    if [ -n "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
        cp "$LITEHTTPD_TP_DEPS_CACHE/libxml2-$VERSION.tar.xz" .
    else
        wget "https://download.gnome.org/sources/libxml2/2.15/libxml2-$VERSION.tar.xz"
    fi
    tar xJf "libxml2-$VERSION.tar.xz"
    ln -sf "libxml2-$VERSION" libxml2
fi
cd libxml2

CPPFLAGS="-fPIC ${CPPFLAGS:-}" ./configure --prefix="$PREFIX" --with-python=no --disable-shared --with-lzma=no
make
if [ ! -f ./.libs/libxml2.a ]; then
    echo "FATAL: libxml2.a not built"
    exit 1
fi
make install
BUILD_LIBXML2_SH
    chmod +x ./build_libxml2.sh

    cat > ./build_yajl.sh <<'BUILD_YAJL_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
mkdir -p "$PREFIX/src"
cd "$PREFIX/src"

rm -rf yajl-2.1.0
if [ -n "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    cp "$LITEHTTPD_TP_DEPS_CACHE/yajl-2.1.0.tar.gz" .
else
    wget https://github.com/lloyd/yajl/archive/refs/tags/2.1.0.tar.gz -O yajl-2.1.0.tar.gz
fi
tar -xzf yajl-2.1.0.tar.gz
cd yajl-2.1.0
./configure -p "$PREFIX"

cd build
cmake -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_C_FLAGS="-fPIC" ..
make install
cd ../../..
cp patches/libtool/libyajl.la lib/libyajl.la
echo "libdir='$PREFIX/lib'" >> lib/libyajl.la
mv lib/libyajl_s.a lib/libyajl.a
rm -f lib/libyajl.so*
BUILD_YAJL_SH
    chmod +x ./build_yajl.sh

    cat > ./build_expat.sh <<'BUILD_EXPAT_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
mkdir -p "$PREFIX/src"
cd "$PREFIX/src"

if [ ! -d libexpat ]; then
    git clone https://github.com/libexpat/libexpat.git
fi
cd libexpat
if [ -z "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    git fetch --tags --force || true
    git checkout R_2_2_9
fi
cd expat

rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DEXPAT_SHARED_LIBS=OFF \
    -DEXPAT_BUILD_DOCS=OFF \
    -DEXPAT_BUILD_EXAMPLES=OFF \
    -DEXPAT_BUILD_TESTS=OFF \
    -DEXPAT_BUILD_TOOLS=OFF \
    ..
cmake --build . --target install -j"${LITEHTTPD_BUILD_JOBS:-$(nproc)}"
BUILD_EXPAT_SH
    chmod +x ./build_expat.sh

    cat > ./build_ip2loc.sh <<'BUILD_IP2LOC_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
cd src/ip2loc

if [ -z "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    git reset --hard
    git fetch --tags --force || true
    git checkout 8.6.1
fi

autoreconf -i -v --force
./configure --prefix="$PREFIX" -enable-shared=no
make install
BUILD_IP2LOC_SH
    chmod +x ./build_ip2loc.sh

    cat > ./build_libmaxminddb.sh <<'BUILD_LIBMAXMINDDB_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
cd src/libmaxminddb

if [ -z "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    git reset --hard
    git fetch --tags --force || true
    git checkout 1.12.2
fi

./bootstrap
CPPFLAGS="-I../include" ./configure --disable-tests --prefix="$PREFIX" -enable-shared=no --disable-binaries
make install
cp src/libmaxminddb.la "$PREFIX/lib"
cp include/maxminddb_config.h "$PREFIX/include"
BUILD_LIBMAXMINDDB_SH
    chmod +x ./build_libmaxminddb.sh

    cat > ./build_lmdb.sh <<'BUILD_LMDB_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
mkdir -p "$PREFIX/src"
cd "$PREFIX/src"

REMOTE_URL='https://github.com/LMDB/lmdb.git'
if [ -n "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    rm -rf lmdb
    tar xzf "$LITEHTTPD_TP_DEPS_CACHE/lmdb-0c8b94738866b1ba7ffac27287fd6cab83f2ade4.tar.gz"
    mv lmdb-0c8b94738866b1ba7ffac27287fd6cab83f2ade4 lmdb
else
    if [ -d "lmdb" ]; then
        CUR_REMOTE=$(git config --file=./lmdb/.git/config --get remote.origin.url)
        if [ "${REMOTE_URL}" != "${CUR_REMOTE}" ]; then
            rm -rf ./lmdb
        fi
    fi
    if [ ! -d "lmdb" ]; then
        git clone --depth 1 --branch mdb.master "$REMOTE_URL" lmdb
    fi
    cd lmdb
    git checkout --force mdb.master
    git fetch --tags --force || true
    cd ..
fi

cd lmdb/libraries/liblmdb/
XCFLAGS=-fPIC make
if [ ! -f liblmdb.a ]; then
    echo "FATAL: liblmdb.a not built"
    exit 1
fi
cp *.h ../../../../include
cp *.a ../../../../lib
cd ../../../../
cp patches/libtool/liblmdb.la lib/liblmdb.la
echo "libdir='$PREFIX/lib'" >> lib/liblmdb.la
BUILD_LMDB_SH
    chmod +x ./build_lmdb.sh

    # Upstream build_curl.sh clones the full curl repository and then checks
    # out the curl-7_88_1 tag. Fetch only that tag so release builds do not
    # depend on a large mutable history download.
    cat > ./build_curl.sh <<'BUILD_CURL_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
mkdir -p "$PREFIX/src"
cd "$PREFIX/src"

CURL_TAG=curl-7_88_1
if [ -n "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    rm -rf curl
    tar xzf "$LITEHTTPD_TP_DEPS_CACHE/curl-$CURL_TAG.tar.gz"
    mv "curl-$CURL_TAG" curl
elif [ ! -d curl/.git ]; then
    rm -rf curl
    clone_rc=0
    git clone --depth 1 --branch "$CURL_TAG" https://github.com/curl/curl.git curl ||
        clone_rc=$?
    if [ "$clone_rc" -ne 0 ]; then
        rm -rf curl
        mkdir curl
        git -C curl init
        git -C curl remote add origin https://github.com/curl/curl.git
        git -C curl fetch --depth 1 origin "refs/tags/$CURL_TAG"
        git -C curl checkout --detach FETCH_HEAD
    fi
fi

cd curl
if [ -z "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    git remote remove origin 2>/dev/null || true
    git remote add origin https://github.com/curl/curl.git
    git fetch --depth 1 origin "refs/tags/$CURL_TAG" || true
    git checkout --force "$CURL_TAG" || git checkout --detach FETCH_HEAD
    git reset --hard
fi

./buildconf
CFLAGS="-fPIC" LIBS=-lpthread ./configure --prefix="$PREFIX" --with-ssl="$PREFIX" --with-brotli=no --enable-shared=no --enable-ldap=no --without-nghttp2 --without-nghttp3
make -j"${LITEHTTPD_BUILD_JOBS:-$(nproc)}"
make install
BUILD_CURL_SH
    chmod +x ./build_curl.sh

    # Upstream build_libmodsec.sh performs a full clone and then checks out the
    # v3.0.15 tag. On slow CI networks this can hang for a long time before the
    # useful checkout even starts. Fetch the exact tag shallowly and keep the
    # same configure flags.
    cat > ./build_libmodsec.sh <<'BUILD_LIBMODSEC_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
mkdir -p "$PREFIX/src"
cd src

if [ ! -d yajl-2.1.0 ]; then
    ../script/build_yajl.sh
    cd src
fi

if [ -n "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    rm -rf ModSecurity
    tar xzf "$LITEHTTPD_TP_DEPS_CACHE/modsecurity-v3.0.15-recursive.tar.gz"
elif [ ! -d ModSecurity/.git ]; then
    rm -rf ModSecurity
    clone_rc=0
    git clone --depth 1 --branch v3.0.15 https://github.com/SpiderLabs/ModSecurity ModSecurity ||
        clone_rc=$?
    if [ "$clone_rc" -ne 0 ]; then
        rm -rf ModSecurity
        mkdir ModSecurity
        git -C ModSecurity init
        git -C ModSecurity remote add origin https://github.com/SpiderLabs/ModSecurity
        git -C ModSecurity fetch --depth 1 origin refs/tags/v3.0.15
        git -C ModSecurity checkout --detach FETCH_HEAD
    fi
fi

cd ModSecurity
if [ -n "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    grep -Fxq "ModSecurity %{modsec_commit}" .litehttpd-source-manifest
    grep -Fxq "bindings/python %{modsec_python_commit}" .litehttpd-source-manifest
    grep -Fxq "others/libinjection %{modsec_libinjection_commit}" .litehttpd-source-manifest
    grep -Fxq "others/mbedtls %{modsec_mbedtls_commit}" .litehttpd-source-manifest
    grep -Fxq "others/mbedtls/framework %{modsec_mbedtls_framework_commit}" .litehttpd-source-manifest
    grep -Fxq "others/mbedtls/tf-psa-crypto %{modsec_tf_psa_crypto_commit}" .litehttpd-source-manifest
    grep -Fxq "others/mbedtls/tf-psa-crypto/framework %{modsec_mbedtls_framework_commit}" .litehttpd-source-manifest
    grep -Fxq "others/mbedtls/tf-psa-crypto/drivers/pqcp/mldsa-native %{modsec_mldsa_native_commit}" .litehttpd-source-manifest
    grep -Fxq "test/test-cases/secrules-language-tests %{modsec_secrules_tests_commit}" .litehttpd-source-manifest
else
    git reset --hard
    git fetch --depth 1 origin refs/tags/v3.0.15 || true
    git checkout --force v3.0.15 || git checkout --detach FETCH_HEAD
fi

export PATH=/home/build/tools/bin/:$PATH
sh build.sh
if [ -z "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    rm -rf others/mbedtls
    git submodule update --init --recursive --depth 1 --jobs 1 ||
        git submodule update --init --recursive --jobs 1
fi
CPPFLAGS="-fPIC ${CPPFLAGS:-}" ./configure --with-yajl=$PREFIX --with-maxmind=$PREFIX --disable-shared --enable-static --with-pcre2=$PREFIX --with-curl=$PREFIX --with-libxml=$PREFIX --with-lmdb=$PREFIX --with-lua=$PREFIX --disable-examples
make -j"${LITEHTTPD_BUILD_JOBS:-$(nproc)}"

cp -r headers/modsecurity ../../include/
cp src/.libs/libmodsecurity.a ../../lib/
BUILD_LIBMODSEC_SH
    chmod +x ./build_libmodsec.sh

    # Upstream build_bssl.sh clones the entire BoringSSL history and then
    # checks out a pinned commit. That is slow and brittle in CI, especially
    # on aarch64/QEMU legs. Keep the same pinned commit and build steps, but
    # fetch only the required object graph.
    cat > ./build_bssl.sh <<'BUILD_BSSL_SH'
#!/bin/sh
set -eu

cd "$(dirname "$0")"
cd ..
PREFIX="$(pwd)"
mkdir -p "$PREFIX/src" "$PREFIX/lib" "$PREFIX/include"
cd "$PREFIX/src"

BSSL_COMMIT=9fc1c33e9c21439ce5f87855a6591a9324e569fd
if [ -n "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    rm -rf boringssl
    tar xzf "$LITEHTTPD_TP_DEPS_CACHE/boringssl-$BSSL_COMMIT.tar.gz"
    mv "boringssl-$BSSL_COMMIT" boringssl
elif [ ! -d "boringssl/.git" ]; then
    rm -rf boringssl
    mkdir boringssl
    git -C boringssl init
fi

cd boringssl
if [ -z "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    git remote remove origin 2>/dev/null || true
    git remote add origin https://github.com/google/boringssl.git
    git fetch --depth 1 origin "$BSSL_COMMIT"
    git checkout --force FETCH_HEAD
    git reset --hard "$BSSL_COMMIT"
fi

rm -rf build

patch -p1 < ../../patches/boringssl/bssl_lstls.patch
patch -p1 < ../../patches/boringssl/bssl_inttypes.patch
sed -i -e "s/-Werror//" CMakeLists.txt

mkdir build
cd build
if [ "$(uname -m)" = "aarch64" ]; then
    cmake ../ -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_FLAGS="-fPIC" -DCMAKE_CXX_FLAGS="-fPIC" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
else
    cmake ../ -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_FLAGS="-fPIC" -DCMAKE_CXX_FLAGS="-fPIC"
fi
cmake --build . --target crypto -j"${LITEHTTPD_BUILD_JOBS:-4}"
cmake --build . --target ssl -j"${LITEHTTPD_BUILD_JOBS:-4}"
cmake --build . --target decrepit -j"${LITEHTTPD_BUILD_JOBS:-4}"

cp crypto/libcrypto.a ../../../lib
cp ssl/libssl.a ../../../lib
cp decrepit/libdecrepit.a ../../../lib

cd ..
cp -r include/openssl ../../include/
BUILD_BSSL_SH
    chmod +x ./build_bssl.sh

    BUILD_LIBS_LINE=$(grep '^BUILD_LIBS=' ./build_ols.sh)
    cat > ./build_ols.sh <<'BUILD_OLS_SH'
#!/bin/sh
set -eu
BUILD_LIBS=""

cd "$(dirname "$0")"
cd ..

mkdir -p lib64
export GIT_TERMINAL_PROMPT=0
export GIT_HTTP_LOW_SPEED_LIMIT="${GIT_HTTP_LOW_SPEED_LIMIT:-32768}"
export GIT_HTTP_LOW_SPEED_TIME="${GIT_HTTP_LOW_SPEED_TIME:-120}"

retry_cmd()
{
    label="$1"
    shift
    rc=0
    for attempt in 1 2 3
    do
        if "$@"; then
            return 0
        else
            rc=$?
        fi
        echo "WARN: ${label} failed (attempt ${attempt}/3, rc=${rc})" >&2
        sleep $((attempt * 10))
    done
    return "$rc"
}

if [ -n "${LITEHTTPD_TP_DEPS_CACHE:-}" ]; then
    for repo in src/brotli src/libexpat src/ip2loc src/libmaxminddb src/luajit src/udns
    do
        [ -d "$repo" ] || { echo "FATAL: source-cache third-party submodule missing: $repo"; exit 1; }
    done
else
    retry_cmd "third-party submodule update" git submodule update --init --depth 1 --recommend-shallow --jobs 1
    for repo in src/libexpat src/libinjection src/ip2loc src/libmaxminddb
    do
        if [ -d "$repo" ]; then
            git -C "$repo" fetch --tags --force || true
        fi
    done
fi
cd script

for BUILD_LIB in $BUILD_LIBS
do
    retry_cmd "build_${BUILD_LIB}" sh -e "./build_${BUILD_LIB}.sh"
done
BUILD_OLS_SH
    sed -i "s|^BUILD_LIBS=.*|$BUILD_LIBS_LINE|" ./build_ols.sh
    chmod +x ./build_ols.sh
    git config --global http.version HTTP/1.1 || true
    git config --global http.lowSpeedLimit 32768 || true
    git config --global http.lowSpeedTime 120 || true
    ./build_ols.sh
    cd ../..
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

# 4. Skip build.sh package installation. RPM BuildRequires and the builder
# image own dependency installation; letting upstream run dnf/yum here makes
# release builds network-mutable and can hang inside rpmbuild.
sed -i 's/^\([[:space:]]*\)prepareLinux[[:space:]]*$/\1echo "prepareLinux skipped for rpmbuild (BuildRequires already installed)"/' build.sh
grep -q 'prepareLinux skipped for rpmbuild' build.sh || { echo "FATAL: prepareLinux skip sed failed"; exit 1; }

# 5. Disable cmake GIT_SUBMODULE (tarball is not a git repo)
sed -i 's/-DMOD_LUA=\$MOD_LUA/-DMOD_LUA=$MOD_LUA -DGIT_SUBMODULE=OFF/' build.sh

BUILD_LOG=%{_builddir}/openlitespeed-build.log
build_sh_rc=0
bash build.sh > "$BUILD_LOG" 2>&1 || build_sh_rc=$?
if [ "$build_sh_rc" -ne 0 ]; then
    echo "WARN: build.sh exited with rc=$build_sh_rc"
    tail -80 "$BUILD_LOG"
    if [ ! -e dist/bin/openlitespeed ] && [ ! -e build/src/openlitespeed ]; then
        echo "FATAL: build.sh failed before producing the OpenLiteSpeed binary"
        exit "$build_sh_rc"
    fi
fi

# build.sh can report failure due to optional modules (pagespeed, lsrecaptcha)
# after the core binary is built. If not in dist/bin/, try direct make.
if [ ! -e dist/bin/openlitespeed ]; then
    echo "=== build.sh did not produce dist/bin/openlitespeed; build log tail ==="
    tail -120 "$BUILD_LOG"
    echo "build.sh did not produce binary, running make directly..."
    MAKE_LOG=%{_builddir}/openlitespeed-make-openlitespeed.log
    make_rc=0
    make -C build -j"${LITEHTTPD_BUILD_JOBS:-$(nproc)}" openlitespeed > "$MAKE_LOG" 2>&1 || make_rc=$?
    if [ "$make_rc" -ne 0 ]; then
        echo "WARN: make openlitespeed exited with rc=$make_rc"
        tail -120 "$MAKE_LOG"
    fi
    mkdir -p dist/bin dist/modules
    cp build/src/openlitespeed dist/bin/ 2>/dev/null || true
    # Copy any .so modules that were successfully built
    find build/src/modules -name "*.so" -exec cp {} dist/modules/ \; 2>/dev/null || true
fi
[ -e dist/bin/openlitespeed ] || { echo "FATAL: OpenLiteSpeed binary was not produced"; exit 1; }

# Verify patches survived the build (CRITICAL: build.sh/install.sh can overwrite)
echo "=== Verifying patches in compiled binary ==="
PATCH_CHECK=$(strings dist/bin/openlitespeed | grep -c "set_php_config_value")
echo "set_php_config_value: $PATCH_CHECK occurrences"
if [ "$PATCH_CHECK" -lt 1 ]; then
    echo "WARNING: Patch 0001 (PHPConfig) not found in binary!"
    echo "Attempting to rebuild from cmake..."
    MAKE_LOG=%{_builddir}/openlitespeed-make-patch-rebuild.log
    make -C build -j"${LITEHTTPD_BUILD_JOBS:-$(nproc)}" openlitespeed > "$MAKE_LOG" 2>&1 || \
        { echo "FATAL: patch rebuild make failed"; tail -120 "$MAKE_LOG"; exit 1; }
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
# Build the network-facing module with exploit-mitigation flags. CMakeLists.txt
# already applies stack protector / FORTIFY / RELRO; passing %optflags here adds
# the distro's hardening on top (this spec disables brp-* post-processing, so
# nothing reintroduces these flags otherwise).
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
      -DPCRE2_ROOT="%{_builddir}/third-party" \
      -DCMAKE_C_FLAGS="%{optflags} -DNDEBUG" 2>&1 | tail -3
cmake --build build --target litehttpd_htaccess -j"${LITEHTTPD_BUILD_JOBS:-$(nproc)}" 2>&1 | tail -3
[ -f build/litehttpd_htaccess.so ] || { echo "FATAL: Module build failed!"; exit 1; }
strip build/litehttpd_htaccess.so
cp build/litehttpd_htaccess.so $SAVE_DIR/litehttpd_htaccess.so
echo "=== Module .so built: $(ls -lh $SAVE_DIR/litehttpd_htaccess.so | awk '{print $5}') ==="

%install
sed -i "s#SERVERROOT=%{install_dir}#SERVERROOT=%{buildroot}%{install_dir}#" ols.conf
sed -i -e 's:USE_LSPHP7=yes:USE_LSPHP7=no:g' install.sh

%if %{with source_cache}
# Seed the WebAdmin PHP build with pinned archives. The upstream build script
# already reuses a saved PHP archive; below we also patch it to keep the saved
# LSAPI archive instead of deleting and re-downloading it.
PHP_BUILD_CACHE=%{buildroot}%{install_dir}/phpbuild
mkdir -p "$PHP_BUILD_CACHE"
cp -f %{SOURCE11} "$PHP_BUILD_CACHE/php-%{php_version}.tar.gz"
cp -f %{SOURCE12} "$PHP_BUILD_CACHE/php-litespeed-%{lsapi_version}.tgz"
echo "%{php_src_sha256}  $PHP_BUILD_CACHE/php-%{php_version}.tar.gz" | sha256sum -c -
echo "%{lsapi_src_sha256}  $PHP_BUILD_CACHE/php-litespeed-%{lsapi_version}.tgz" | sha256sum -c -
%endif

# Upstream install.sh downloads prebuilt admin_php before trying a local build.
# On aarch64 that binary requires GLIBC_2.29, which breaks the EL8-built full
# RPM/tgz. build.sh wraps dist/install.sh into top-level install.sh and moves
# the real installer to dist/_in.sh, so patch every possible generated path and
# fail closed if the download branch was not found.
adminphp_installer_patched=n
for adminphp_installer in install.sh dist/install.sh dist/_in.sh; do
    [ -f "$adminphp_installer" ] || continue
    if grep -Fq 'www.litespeedtech.com/packages/lsphp' "$adminphp_installer"; then
        sed -i \
            -e 's/if \[ "x\$OS" = "xLinux" \] ; then/if false \&\& [ "x$OS" = "xLinux" ] ; then/' \
            "$adminphp_installer"
        grep -Fq 'if false && [ "x$OS" = "xLinux" ] ; then' "$adminphp_installer" && \
            adminphp_installer_patched=y
    fi
done
[ "$adminphp_installer_patched" = y ] || \
    { echo "FATAL: admin_php installer download path was not patched"; exit 1; }

# Keep rpmbuild installation hermetic with respect to the build host. Upstream
# install.sh is designed for interactive server installation and otherwise
# probes public IP, installs host packages, registers systemd units, and can
# update webcache helper code from LiteSpeed's shared-code endpoint.
for generated_installer in install.sh dist/install.sh dist/_in.sh; do
    [ -f "$generated_installer" ] || continue
    sed -i \
        -e 's#\$DLCMD \$LSWS_HOME/myip http://cyberpanel.sh/?ip#printf "%s\\n" "127.0.0.1" > "$LSWS_HOME/myip"#' \
        -e 's#^[[:space:]]*\$LSWS_HOME/admin/misc/rc-inst.sh[[:space:]]*$#        echo "service registration skipped for rpmbuild"#' \
        -e 's#^[[:space:]]*yum install -y libnsl[[:space:]]*$#            echo "libnsl install skipped for rpmbuild"#' \
        -e 's#^[[:space:]]*\$LSWS_HOME/admin/misc/lscmctl[[:space:]]*--update-lib[[:space:]]*$#echo "webcache helper update skipped for rpmbuild"#' \
        "$generated_installer"
done
for generated_functions in dist/functions.sh functions.sh; do
    [ -f "$generated_functions" ] || continue
    sed -i \
        -e 's#^[[:space:]]*yum -y install libxcrypt-compat libnsl[[:space:]]*$#            echo "libxcrypt-compat libnsl install skipped for rpmbuild"#' \
        -e 's#^[[:space:]]*yum -y install libatomic[[:space:]]*$#            echo "libatomic install skipped for rpmbuild"#' \
        -e 's#^[[:space:]]*apt -y install libatomic1[[:space:]]*$#            echo "libatomic1 install skipped for rpmbuild"#' \
        -e 's#^[[:space:]]*\$LSWS_HOME/admin/misc/rc-inst.sh[[:space:]]*$#                echo "service registration skipped for rpmbuild"#' \
        "$generated_functions"
done
side_effect_check_files=
for side_effect_check_file in install.sh dist/install.sh dist/_in.sh dist/functions.sh functions.sh; do
    [ -f "$side_effect_check_file" ] && side_effect_check_files="$side_effect_check_files $side_effect_check_file"
done
if [ -n "$side_effect_check_files" ] && \
    grep -RInE 'cyberpanel\.sh/\?ip|^[[:space:]]*(yum -y install (libxcrypt-compat libnsl|libatomic)|apt -y install libatomic1|yum install -y libnsl)[[:space:]]*$|lscmctl[[:space:]]+--update-lib|^[[:space:]]*\$LSWS_HOME/admin/misc/rc-inst\.sh[[:space:]]*$' \
        $side_effect_check_files; then
    echo "FATAL: rpmbuild-host network/package/service side effect patch failed"
    exit 1
fi

# Fix the bundled PHP 7.4/LSAPI build script so the litespeed SAPI is actually
# enabled when the installer falls through to the local admin_php build.
for adminphp_build in dist/admin/misc/build_admin_php.sh admin/misc/build_admin_php.sh; do
    [ -f "$adminphp_build" ] || continue
    sed -i \
        -e 's/--with-litespeed/--enable-litespeed/g' \
        -e 's/--enable-xml /--enable-xml --with-libxml /g' \
        -e 's/[[:space:]]--with-libexpat-dir=\/usr//g' \
        -e 's/BUILDCONF_FORCE=N/BUILDCONF_FORCE=Y/g' \
        -e 's/cat \/dev\/null ${LOG_FILE}/: > ${LOG_FILE}/g' \
        -e 's/^mkdir ${PHP_BUILD_DIR}$/mkdir -p ${PHP_BUILD_DIR}/g' \
        "$adminphp_build"
    awk '
        $0 == "if [ -e php-litespeed-${LSAPI_VERSION}.tgz ] ; then" {
            print "if [ -e php-litespeed-${LSAPI_VERSION}.tgz ] ; then"
            print "\tmain_msg \"php-litespeed-${LSAPI_VERSION}.tgz already downloaded, use the saved copy.\""
            print "else"
            print "\tDOWNLOAD_URL=\"http://www.litespeedtech.com/packages/lsapi/php-litespeed-${LSAPI_VERSION}.tgz\""
            print "\tmain_msg \"Retrieving LSAPI from ${DOWNLOAD_URL}\""
            print "\t${DL_METHOD} \"php-litespeed-${LSAPI_VERSION}.tgz\" ${DOWNLOAD_URL}"
            print "\tcheck_errs $? \"Could not retrieve LSAPI archive\""
            print "fi"
            skip = 1
            next
        }
        skip {
            if ($0 == "check_errs $? \"Could not retrieve LSAPI archive\"") {
                skip = 0
            }
            next
        }
        { print }
    ' "$adminphp_build" > "$adminphp_build.patched"
    mv -f "$adminphp_build.patched" "$adminphp_build"
    chmod 755 "$adminphp_build"
    grep -Fq 'mkdir -p ${PHP_BUILD_DIR}' "$adminphp_build" || \
        { echo "FATAL: admin_php build script mkdir -p patch failed"; exit 1; }
    grep -Fq 'already downloaded, use the saved copy' "$adminphp_build" || \
        { echo "FATAL: admin_php LSAPI cache patch failed"; exit 1; }
done
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

ADMINPHP=%{buildroot}%{install_dir}/admin/fcgi-bin/admin_php
LSPHP5=%{buildroot}%{install_dir}/fcgi-bin/lsphp5
[ -x "$ADMINPHP" ] || { echo "FATAL: admin_php missing or not executable"; exit 1; }
[ -x "$LSPHP5" ] || { echo "FATAL: lsphp5 missing or not executable"; exit 1; }
"$ADMINPHP" -v 2>&1 | grep -q "PHP 7.4.33 (litespeed)" || \
    { echo "FATAL: admin_php is not the locally built litespeed PHP 7.4.33"; exit 1; }
"$LSPHP5" -v >/dev/null 2>&1 || \
    { echo "FATAL: bundled lsphp5 cannot run on this build host"; exit 1; }
if readelf -V "$ADMINPHP" 2>/dev/null | grep -Eq 'GLIBC_2\.(29|[3-9][0-9])'; then
    echo "FATAL: admin_php requires GLIBC newer than EL8 provides"
    readelf -V "$ADMINPHP" | grep -o 'GLIBC_[0-9]\+\.[0-9]\+' | sort -uV
    exit 1
fi
echo "=== admin_php verification passed ==="

# The upstream installer creates an adminpasswd file during rpmbuild. Shipping
# that file would publish one shared WebAdmin credential across every RPM/tgz
# built from this artifact.
rm -f %{buildroot}%{install_dir}/adminpasswd
[ ! -e %{buildroot}%{install_dir}/adminpasswd ] || \
    { echo "FATAL: build-time WebAdmin password would be packaged"; exit 1; }

# Do not ship PHP source/build artifacts in full RPM/tgz. The installer leaves
# phpbuild behind after compiling WebAdmin PHP; runtime only needs admin_php and
# fcgi-bin/lsphp5.
rm -rf %{buildroot}%{install_dir}/phpbuild

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
# Strategy: only run on fresh install ($1==1) with grep guards for idempotency.
# On upgrade, user's config is preserved by %config(noreplace).
CONF=%{install_dir}/conf/httpd_config.conf
VHCONF=%{install_dir}/conf/vhosts/Example/vhconf.conf

# Only run on fresh install ($1 == 1), not upgrade.
# On upgrade, user's config is preserved by %config(noreplace).
if [ "$1" -eq 1 ]; then
    # Auto-enable litehttpd module: only if module block not already in config
    if [ -f "$CONF" ] && [ -f "%{install_dir}/modules/litehttpd_htaccess.so" ]; then
        if ! grep -q "^module litehttpd_htaccess" "$CONF" 2>/dev/null; then
            printf '\nmodule litehttpd_htaccess {\n  ls_enabled 1\n}\n' >> "$CONF"
        fi
    fi

    # Enable rewrite in vhost (match both "rewrite {" and "rewrite  {")
    if [ -f "$VHCONF" ]; then
        sed -i '/^rewrite\s*{/,/^}/s/enable 0/enable 1/' "$VHCONF" 2>/dev/null || true
    fi

    # Add index.php to indexFiles line (only if that specific line exists without index.php)
    if [ -f "$VHCONF" ]; then
        sed -i '/indexFiles/{ /index\.php/!s/indexFiles.*/indexFiles index.php, index.html/ }' "$VHCONF" 2>/dev/null || true
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
%config(noreplace) %{install_dir}/conf/httpd_config.conf
%config(noreplace) %{install_dir}/conf/vhosts/
%{install_dir}/*
/etc/init.d/lsws
%{_unitdir}/lshttpd.service

%changelog
* Thu Jul 02 2026 LiteHTTPD <noreply@web.casa> - 1.9.1-4.litehttpd
- Add pinned source-cache mode for release builds:
  - litespeedtech/third-party pinned to commit %{third_party_commit}
  - PHP %{php_version} and php-litespeed %{lsapi_version} archives verified by SHA256
  - WebAdmin PHP build reuses cached PHP/LSAPI archives when provided
- Tighten build.sh error handling so real OLS build failures are not swallowed.
- Remove build-time generated WebAdmin adminpasswd from RPM/tgz artifacts.

* Wed Jul 01 2026 LiteHTTPD <noreply@web.casa> - 1.9.1-1.litehttpd
- Rebase patched OpenLiteSpeed build to v1.9.1:
  - upstream HTTP/2 and HTTP/3 protocol hardening
  - upstream general code hardening
  - upstream PCRE2 regex engine migration
- Fix RewriteRule LSIAPI guard so patched .htaccess RewriteRule execution
  is not disabled by an unconditional NULL return.

* Thu Jun 11 2026 LiteHTTPD <noreply@web.casa> - 1.8.5-3.litehttpd
- Security hardening (litehttpd module 2.1.4):
  - Fix Require user/group auth fail-open; Require user enforced, unknown
    Require forms fail closed (also inside RequireAny/RequireAll)
  - Confine AuthUserFile to the document root (O_NOFOLLOW + TOCTOU re-check)
  - BruteForceTrustedProxy gates X-Forwarded-For (anti-spoof)
  - SHM private segment names + magic guard; expression DoS/ReDoS guards
  - Build with stack-protector-strong, FORTIFY, full RELRO, noexecstack

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
