#!/usr/bin/env bash
# Create the source cache consumed by openlitespeed-litehttpd.spec.

set -euo pipefail

OUT_DIR="${1:-/tmp}"
THIRD_PARTY_COMMIT="${THIRD_PARTY_COMMIT:-7297c534b844d808fce7ec42798ef2b1f3aa5259}"
PHP_VERSION="${PHP_VERSION:-7.4.33}"
LSAPI_VERSION="${LSAPI_VERSION:-8.1}"
PHP_SHA256="${PHP_SHA256:-5a2337996f07c8a097e03d46263b5c98d2c8e355227756351421003bea8f463e}"
LSAPI_SHA256="${LSAPI_SHA256:-aa79814b939a0877d83910e11d21255a801842f0fca0796fde970774f41c296e}"
GIT_SUBMODULE_JOBS="${GIT_SUBMODULE_JOBS:-1}"
PSOL_VERSION="${PSOL_VERSION:-1.11.33.4}"
BORINGSSL_COMMIT="${BORINGSSL_COMMIT:-9fc1c33e9c21439ce5f87855a6591a9324e569fd}"
CURL_TAG="${CURL_TAG:-curl-7_88_1}"
LMDB_COMMIT="${LMDB_COMMIT:-0c8b94738866b1ba7ffac27287fd6cab83f2ade4}"
ZLIB_COMMIT="${ZLIB_COMMIT:-e3dc0a85b7032e98380dec011bc8f2c2ee0d8fca}"
LIBBCRYPT_COMMIT="${LIBBCRYPT_COMMIT:-55ff64349dec3012cfbbb1c4f92d4dbd46920213}"
MODSEC_VERSION="${MODSEC_VERSION:-3.0.15}"
MODSEC_COMMIT="${MODSEC_COMMIT:-0fb4aff98b4980cf6426697d5605c424e3d5bb60}"
MODSEC_PYTHON_COMMIT="${MODSEC_PYTHON_COMMIT:-bc625d5bb0bac6a64bcce8dc9902208612399348}"
MODSEC_LIBINJECTION_COMMIT="${MODSEC_LIBINJECTION_COMMIT:-211782219663f889f471650150df12b623c5766e}"
MODSEC_MBEDTLS_COMMIT="${MODSEC_MBEDTLS_COMMIT:-0fe989b6b514192783c469039edd325fd0989806}"
MODSEC_MBEDTLS_FRAMEWORK_COMMIT="${MODSEC_MBEDTLS_FRAMEWORK_COMMIT:-dff9da04438d712f7647fd995bc90fadd0c0e2ce}"
MODSEC_TF_PSA_CRYPTO_COMMIT="${MODSEC_TF_PSA_CRYPTO_COMMIT:-29160dd877d29658279fd683b2ae57b320ddcf09}"
MODSEC_MLDSA_NATIVE_COMMIT="${MODSEC_MLDSA_NATIVE_COMMIT:-5772b4f4a0105694b1203abb582273f78fa951b7}"
MODSEC_SECRULES_TESTS_COMMIT="${MODSEC_SECRULES_TESTS_COMMIT:-a3d4405e5a2c90488c387e589c5534974575e35b}"

# libaio 0.3.113: pagure.io removed its archive endpoints (404 since
# 2026-10); Debian's orig tarball is the same upstream git-tag tree
# (verified by full tree-diff) served from a stable CDN.
LIBAIO_SHA256="${LIBAIO_SHA256:-2c44d1c5fd0d43752287c9ae1eb9c023f04ef848ea8d4aafa46e9aedb678200b}"
PCRE_SHA256="${PCRE_SHA256:-4e6ce03e0336e8b4a3d6c2b70b1c5e18590a5673a98186da90d4f33c23defc09}"
PCRE2_SHA256="${PCRE2_SHA256:-0e138387df7835d7403b8351e2226c1377da804e0737db0e071b48f07c9d12ee}"
PSOL_SHA256="${PSOL_SHA256:-97408b04bf7395ab34d7b5baed95ae3dc928746213ca02590773b66a7697a7dc}"
LIBXML2_SHA256="${LIBXML2_SHA256:-78262a6e7ac170d6528ebfe2efccdf220191a5af6a6cd61ea4a9a9a5042c7a07}"
YAJL_SHA256="${YAJL_SHA256:-3fb73364a5a30efe615046d07e6db9d09fd2b41c763c5f7d3bfb121cd5c5ac5a}"
BORINGSSL_SHA256="${BORINGSSL_SHA256:-3e2a15d002634e4cfd44a74be286ed99c8ed138118479181a3099e16913e3942}"
CURL_SHA256="${CURL_SHA256:-eb9f2ca79e2c39b89827cf2cf21f39181f6a537f50dc1df9c33d705913009ac4}"
LMDB_SHA256="${LMDB_SHA256:-b4063bdc2b7ab7e2c1df0523048a2b0302c0b900135ec8d7e86fae698fe5ed09}"
ZLIB_SHA256="${ZLIB_SHA256:-33356dac6140d584347fe46bcf7083bd949dec49ac4b52417ae334ec70e3dbc3}"
LIBBCRYPT_SHA256="${LIBBCRYPT_SHA256:-ebe28e4791f5ad778b95ad7c3edff158bcc476d07d1503dd82fec1fad2a47f88}"

mkdir -p "$OUT_DIR"

tmp_root=""
cleanup() {
    if [ -n "$tmp_root" ]; then
        rm -rf "$tmp_root"
    fi
}
trap cleanup EXIT

fetch_url() {
    local url="$1"
    local dest="$2"

    if command -v wget >/dev/null 2>&1; then
        wget -q -O "$dest" "$url"
    elif command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$dest" "$url"
    else
        echo "FATAL: wget or curl is required" >&2
        return 1
    fi
}

verify_sha256() {
    local expected="$1"
    local file="$2"

    echo "${expected}  ${file}" | sha256sum -c -
}

verify_third_party_archive() {
    local archive="$1"
    local verify_root
    local cache_commit

    verify_root="$(mktemp -d)"
    tar xzf "$archive" -C "$verify_root"
    if [ -d "$verify_root/third-party/script" ]; then
        cache_commit="$(git -C "$verify_root/third-party" rev-parse HEAD 2>/dev/null || true)"
    elif [ -f "$verify_root/script/build_ols.sh" ]; then
        cache_commit="$(git -C "$verify_root" rev-parse HEAD 2>/dev/null || true)"
    else
        cache_commit=""
    fi
    rm -rf "$verify_root"

    if [ "$cache_commit" != "$THIRD_PARTY_COMMIT" ]; then
        echo "FATAL: third-party cache commit mismatch: ${cache_commit:-unknown}" >&2
        return 1
    fi
}

verify_third_party_deps_archive() {
    local archive="$1"
    local verify_root
    local cache_root
    local verify_status=0

    verify_root="$(mktemp -d)"
    tar xzf "$archive" -C "$verify_root"
    if [ -f "$verify_root/third-party-deps-src/SHA256SUMS" ]; then
        cache_root="$verify_root/third-party-deps-src"
    else
        cache_root="$verify_root"
    fi

    if [ ! -f "$cache_root/SHA256SUMS" ] || [ ! -f "$cache_root/MANIFEST" ]; then
        echo "FATAL: third-party deps cache missing SHA256SUMS or MANIFEST" >&2
        rm -rf "$verify_root"
        return 1
    fi

    (
        cd "$cache_root"
        for required_file in \
            libaio-0.3.113.tar.gz \
            pcre-8.45.tar.gz \
            pcre2-10.45.tar.gz \
            "${PSOL_VERSION}.tar.gz" \
            libxml2-2.15.3.tar.xz \
            yajl-2.1.0.tar.gz \
            "boringssl-${BORINGSSL_COMMIT}.tar.gz" \
            "curl-${CURL_TAG}.tar.gz" \
            "lmdb-${LMDB_COMMIT}.tar.gz" \
            "zlib-${ZLIB_COMMIT}.tar.gz" \
            "libbcrypt-${LIBBCRYPT_COMMIT}.tar.gz" \
            "modsecurity-v${MODSEC_VERSION}-recursive.tar.gz"
        do
            [ -f "$required_file" ] || {
                echo "FATAL: third-party deps cache missing $required_file" >&2
                exit 1
            }
        done
        sha256sum -c SHA256SUMS
        grep -Fxq "ModSecurity ${MODSEC_COMMIT}" MANIFEST
        grep -Fxq "bindings/python ${MODSEC_PYTHON_COMMIT}" MANIFEST
        grep -Fxq "others/libinjection ${MODSEC_LIBINJECTION_COMMIT}" MANIFEST
        grep -Fxq "others/mbedtls ${MODSEC_MBEDTLS_COMMIT}" MANIFEST
        grep -Fxq "others/mbedtls/framework ${MODSEC_MBEDTLS_FRAMEWORK_COMMIT}" MANIFEST
        grep -Fxq "others/mbedtls/tf-psa-crypto ${MODSEC_TF_PSA_CRYPTO_COMMIT}" MANIFEST
        grep -Fxq "others/mbedtls/tf-psa-crypto/framework ${MODSEC_MBEDTLS_FRAMEWORK_COMMIT}" MANIFEST
        grep -Fxq "others/mbedtls/tf-psa-crypto/drivers/pqcp/mldsa-native ${MODSEC_MLDSA_NATIVE_COMMIT}" MANIFEST
        grep -Fxq "test/test-cases/secrules-language-tests ${MODSEC_SECRULES_TESTS_COMMIT}" MANIFEST
    ) || verify_status=$?
    rm -rf "$verify_root"
    return "$verify_status"
}

download_checked() {
    local url="$1"
    local dest="$2"
    local expected="$3"
    local tmp_dest

    if [ -s "$dest" ]; then
        echo "Using cached $(basename "$dest")"
        verify_sha256 "$expected" "$dest"
        return 0
    fi

    tmp_dest="${dest}.tmp.$$"
    rm -f "$tmp_dest"
    echo "Downloading $(basename "$dest")"
    fetch_url "$url" "$tmp_dest"
    verify_sha256 "$expected" "$tmp_dest"
    mv -f "$tmp_dest" "$dest"
}

verify_git_head() {
    local repo="$1"
    local expected="$2"
    local label="$3"
    local actual

    actual="$(git -C "$repo" rev-parse HEAD)"
    if [ "$actual" != "$expected" ]; then
        echo "FATAL: ${label} checkout mismatch: $actual" >&2
        return 1
    fi
}

create_third_party_cache() {
    local archive="$OUT_DIR/third-party-src.tar.gz"
    local repo
    local tmp_archive
    local actual_commit

    if [ -s "$archive" ]; then
        echo "Using cached $(basename "$archive")"
        verify_third_party_archive "$archive"
        return 0
    fi

    tmp_root="$(mktemp -d)"
    repo="$tmp_root/third-party"
    tmp_archive="$OUT_DIR/third-party-src.tar.gz.tmp.$$"
    rm -f "$tmp_archive"

    echo "Cloning litespeedtech/third-party at ${THIRD_PARTY_COMMIT}"
    git init "$repo"
    git -C "$repo" remote add origin https://github.com/litespeedtech/third-party.git
    git -C "$repo" fetch --depth 1 origin "$THIRD_PARTY_COMMIT"
    git -C "$repo" checkout --detach "$THIRD_PARTY_COMMIT"
    actual_commit="$(git -C "$repo" rev-parse HEAD)"
    if [ "$actual_commit" != "$THIRD_PARTY_COMMIT" ]; then
        echo "FATAL: third-party checkout mismatch: $actual_commit" >&2
        return 1
    fi

    git -C "$repo" submodule update --init --depth 1 --recommend-shallow --jobs "$GIT_SUBMODULE_JOBS" ||
        git -C "$repo" submodule update --init --recursive --jobs 1

    tar czf "$tmp_archive" \
        --transform="flags=r;s,^\.,third-party," \
        -C "$repo" .
    mv -f "$tmp_archive" "$archive"
    rm -rf "$tmp_root"
    tmp_root=""
}

create_modsecurity_recursive_archive() {
    local archive="$1"
    local repo="$tmp_root/ModSecurity"
    local tmp_archive="${archive}.tmp.$$"

    rm -rf "$repo" "$tmp_archive"
    echo "Cloning ModSecurity v${MODSEC_VERSION} with recursive submodules"
    git clone --depth 1 --branch "v${MODSEC_VERSION}" \
        https://github.com/SpiderLabs/ModSecurity "$repo" ||
        {
            rm -rf "$repo"
            mkdir "$repo"
            git -C "$repo" init
            git -C "$repo" remote add origin https://github.com/SpiderLabs/ModSecurity
            git -C "$repo" fetch --depth 1 origin "refs/tags/v${MODSEC_VERSION}"
            git -C "$repo" checkout --detach FETCH_HEAD
        }
    verify_git_head "$repo" "$MODSEC_COMMIT" "ModSecurity"

    git -C "$repo" submodule update --init --recursive --depth 1 --jobs "$GIT_SUBMODULE_JOBS" ||
        git -C "$repo" submodule update --init --recursive --jobs 1
    verify_git_head "$repo/bindings/python" "$MODSEC_PYTHON_COMMIT" "ModSecurity bindings/python"
    verify_git_head "$repo/others/libinjection" "$MODSEC_LIBINJECTION_COMMIT" "ModSecurity others/libinjection"
    verify_git_head "$repo/others/mbedtls" "$MODSEC_MBEDTLS_COMMIT" "ModSecurity others/mbedtls"
    verify_git_head "$repo/others/mbedtls/framework" "$MODSEC_MBEDTLS_FRAMEWORK_COMMIT" "ModSecurity others/mbedtls/framework"
    verify_git_head "$repo/others/mbedtls/tf-psa-crypto" "$MODSEC_TF_PSA_CRYPTO_COMMIT" "ModSecurity TF-PSA-Crypto"
    verify_git_head "$repo/others/mbedtls/tf-psa-crypto/framework" "$MODSEC_MBEDTLS_FRAMEWORK_COMMIT" "ModSecurity TF-PSA-Crypto framework"
    verify_git_head "$repo/others/mbedtls/tf-psa-crypto/drivers/pqcp/mldsa-native" "$MODSEC_MLDSA_NATIVE_COMMIT" "ModSecurity mldsa-native"
    verify_git_head "$repo/test/test-cases/secrules-language-tests" "$MODSEC_SECRULES_TESTS_COMMIT" "ModSecurity secrules-language-tests"

    cat > "$repo/.litehttpd-source-manifest" <<MANIFEST
ModSecurity ${MODSEC_COMMIT}
bindings/python ${MODSEC_PYTHON_COMMIT}
others/libinjection ${MODSEC_LIBINJECTION_COMMIT}
others/mbedtls ${MODSEC_MBEDTLS_COMMIT}
others/mbedtls/framework ${MODSEC_MBEDTLS_FRAMEWORK_COMMIT}
others/mbedtls/tf-psa-crypto ${MODSEC_TF_PSA_CRYPTO_COMMIT}
others/mbedtls/tf-psa-crypto/framework ${MODSEC_MBEDTLS_FRAMEWORK_COMMIT}
others/mbedtls/tf-psa-crypto/drivers/pqcp/mldsa-native ${MODSEC_MLDSA_NATIVE_COMMIT}
test/test-cases/secrules-language-tests ${MODSEC_SECRULES_TESTS_COMMIT}
MANIFEST

    tar czf "$tmp_archive" \
        --exclude='.git' \
        --exclude='*/.git' \
        --transform="flags=r;s,^\.,ModSecurity," \
        -C "$repo" .
    mv -f "$tmp_archive" "$archive"
}

create_third_party_deps_cache() {
    local archive="$OUT_DIR/third-party-deps-src.tar.gz"
    local cache_root
    local tmp_archive

    if [ -s "$archive" ]; then
        echo "Using cached $(basename "$archive")"
        if verify_third_party_deps_archive "$archive"; then
            return 0
        fi
        echo "Cached $(basename "$archive") is stale; rebuilding"
        rm -f "$archive"
    fi

    tmp_root="$(mktemp -d)"
    cache_root="$tmp_root/third-party-deps-src"
    tmp_archive="$OUT_DIR/third-party-deps-src.tar.gz.tmp.$$"
    mkdir -p "$cache_root"
    rm -f "$tmp_archive"

    download_checked \
        "https://deb.debian.org/debian/pool/main/liba/libaio/libaio_0.3.113.orig.tar.gz" \
        "$cache_root/libaio-0.3.113.tar.gz" \
        "$LIBAIO_SHA256"
    download_checked \
        "https://ftp.exim.org/pub/pcre/pcre-8.45.tar.gz" \
        "$cache_root/pcre-8.45.tar.gz" \
        "$PCRE_SHA256"
    download_checked \
        "https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.45/pcre2-10.45.tar.gz" \
        "$cache_root/pcre2-10.45.tar.gz" \
        "$PCRE2_SHA256"
    download_checked \
        "https://dl.google.com/dl/page-speed/psol/${PSOL_VERSION}.tar.gz" \
        "$cache_root/${PSOL_VERSION}.tar.gz" \
        "$PSOL_SHA256"
    download_checked \
        "https://download.gnome.org/sources/libxml2/2.15/libxml2-2.15.3.tar.xz" \
        "$cache_root/libxml2-2.15.3.tar.xz" \
        "$LIBXML2_SHA256"
    download_checked \
        "https://github.com/lloyd/yajl/archive/refs/tags/2.1.0.tar.gz" \
        "$cache_root/yajl-2.1.0.tar.gz" \
        "$YAJL_SHA256"
    download_checked \
        "https://github.com/google/boringssl/archive/${BORINGSSL_COMMIT}.tar.gz" \
        "$cache_root/boringssl-${BORINGSSL_COMMIT}.tar.gz" \
        "$BORINGSSL_SHA256"
    download_checked \
        "https://github.com/curl/curl/archive/refs/tags/${CURL_TAG}.tar.gz" \
        "$cache_root/curl-${CURL_TAG}.tar.gz" \
        "$CURL_SHA256"
    download_checked \
        "https://github.com/LMDB/lmdb/archive/${LMDB_COMMIT}.tar.gz" \
        "$cache_root/lmdb-${LMDB_COMMIT}.tar.gz" \
        "$LMDB_SHA256"
    download_checked \
        "https://github.com/madler/zlib/archive/${ZLIB_COMMIT}.tar.gz" \
        "$cache_root/zlib-${ZLIB_COMMIT}.tar.gz" \
        "$ZLIB_SHA256"
    download_checked \
        "https://github.com/litespeedtech/libbcrypt/archive/${LIBBCRYPT_COMMIT}.tar.gz" \
        "$cache_root/libbcrypt-${LIBBCRYPT_COMMIT}.tar.gz" \
        "$LIBBCRYPT_SHA256"

    create_modsecurity_recursive_archive "$cache_root/modsecurity-v${MODSEC_VERSION}-recursive.tar.gz"
    cp "$tmp_root/ModSecurity/.litehttpd-source-manifest" "$cache_root/MANIFEST"
    (
        cd "$cache_root"
        sha256sum \
            libaio-0.3.113.tar.gz \
            pcre-8.45.tar.gz \
            pcre2-10.45.tar.gz \
            "${PSOL_VERSION}.tar.gz" \
            libxml2-2.15.3.tar.xz \
            yajl-2.1.0.tar.gz \
            "boringssl-${BORINGSSL_COMMIT}.tar.gz" \
            "curl-${CURL_TAG}.tar.gz" \
            "lmdb-${LMDB_COMMIT}.tar.gz" \
            "zlib-${ZLIB_COMMIT}.tar.gz" \
            "libbcrypt-${LIBBCRYPT_COMMIT}.tar.gz" \
            "modsecurity-v${MODSEC_VERSION}-recursive.tar.gz" \
            > SHA256SUMS
    )

    tar czf "$tmp_archive" -C "$tmp_root" third-party-deps-src
    mv -f "$tmp_archive" "$archive"
    verify_third_party_deps_archive "$archive"
    rm -rf "$tmp_root"
    tmp_root=""
}

create_third_party_cache
create_third_party_deps_cache
download_checked \
    "https://www.php.net/distributions/php-${PHP_VERSION}.tar.gz" \
    "$OUT_DIR/php-${PHP_VERSION}.tar.gz" \
    "$PHP_SHA256"
download_checked \
    "https://www.litespeedtech.com/packages/lsapi/php-litespeed-${LSAPI_VERSION}.tgz" \
    "$OUT_DIR/php-litespeed-${LSAPI_VERSION}.tgz" \
    "$LSAPI_SHA256"

ls -lh \
    "$OUT_DIR/third-party-src.tar.gz" \
    "$OUT_DIR/third-party-deps-src.tar.gz" \
    "$OUT_DIR/php-${PHP_VERSION}.tar.gz" \
    "$OUT_DIR/php-litespeed-${LSAPI_VERSION}.tgz"
