#!/usr/bin/env bash
# Build full openlitespeed-litehttpd RPM/tgz artifacts through the same
# rpmbuild + source-cache path used by .github/workflows/release.yml.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${RELEASE_ARTIFACT_OUT_DIR:-/tmp/ols-p2-artifacts}"
CACHE_DIR="${RELEASE_SOURCE_CACHE_DIR:-/tmp/litehttpd-p2-source-cache}"
ARCH_FILTER="all"
DIST_FILTER="all"
CONTINUE_ON_ERROR="${RELEASE_ARTIFACT_CONTINUE_ON_ERROR:-0}"
FAIL_COUNT=0
DOCKER_CMD=()
ONLY_TARGETS=()

usage() {
    cat <<'USAGE'
Usage: bash scripts/build-release-ols-artifacts.sh [options]

Options:
  --arch x86_64|aarch64|all      Limit CPU architecture. Default: all.
  --dist el8|el9|el10|all        Limit EL dist tag. Default: all.
  --only DIST-ARCH               Build one matrix leg, e.g. el9-aarch64.
                                 Can be repeated.
  --out-dir DIR                  Artifact output dir.
  --cache-dir DIR                Source-cache dir. Default: /tmp/litehttpd-p2-source-cache.
  --continue-on-error            Continue remaining legs after a failure.

Builds full OLS RPMs:
  openlitespeed-litehttpd-<ols-version>-*.{el8,el9,el10}.{x86_64,aarch64}.rpm

For el8 legs, also assembles:
  openlitespeed-litehttpd-<ols-version>-<arch>-linux.tgz
  litehttpd-ols-patch-<ols-version>-<arch>-linux.tgz
  litehttpd-module-<module-version>-<arch>-linux.tgz

The build intentionally runs every dist tag through packaging/openlitespeed-
litehttpd.spec. It never renames or edits an existing RPM to fill the matrix.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --arch)
            [ "$#" -ge 2 ] || {
                usage >&2
                exit 2
            }
            ARCH_FILTER="$2"
            shift 2
            ;;
        --dist)
            [ "$#" -ge 2 ] || {
                usage >&2
                exit 2
            }
            DIST_FILTER="$2"
            shift 2
            ;;
        --only)
            [ "$#" -ge 2 ] || {
                usage >&2
                exit 2
            }
            ONLY_TARGETS+=("$2")
            shift 2
            ;;
        --out-dir)
            [ "$#" -ge 2 ] || {
                usage >&2
                exit 2
            }
            OUT_DIR="$2"
            shift 2
            ;;
        --cache-dir)
            [ "$#" -ge 2 ] || {
                usage >&2
                exit 2
            }
            CACHE_DIR="$2"
            shift 2
            ;;
        --continue-on-error)
            CONTINUE_ON_ERROR=1
            shift
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
done

die() {
    echo "ERROR: $*" >&2
    exit 1
}

spec_global_value() {
    local name="$1"
    local file="$2"

    awk -v name="$name" \
        '$1 == "%global" && $2 == name { print $3; exit }' "$file"
}

spec_version() {
    local file="$1"

    awk '$1 == "Version:" { print $2; exit }' "$file"
}

select_docker() {
    if docker info >/dev/null 2>&1; then
        DOCKER_CMD=(docker)
        return 0
    fi
    if command -v sudo >/dev/null 2>&1 && sudo -n docker info >/dev/null 2>&1; then
        DOCKER_CMD=(sudo -n docker)
        return 0
    fi
    die "Docker daemon is not available to this user and sudo -n docker failed"
}

run_docker() {
    "${DOCKER_CMD[@]}" run --rm "$@"
}

run_step() {
    if "$@"; then
        return 0
    fi

    if [ "$CONTINUE_ON_ERROR" = "1" ]; then
        echo "ERROR: failed: $*" >&2
        FAIL_COUNT=$((FAIL_COUNT + 1))
        return 0
    fi

    return 1
}

fix_output_owner() {
    if [ "${DOCKER_CMD[0]}" = "sudo" ]; then
        sudo -n chown -R "$(id -u):$(id -g)" "$OUT_DIR" >/dev/null 2>&1 || true
    fi
}

want_arch() {
    local arch="$1"

    case "$ARCH_FILTER" in
        all) return 0 ;;
        x86_64 | aarch64) [ "$ARCH_FILTER" = "$arch" ] ;;
        *) die "--arch must be x86_64, aarch64, or all" ;;
    esac
}

want_dist() {
    local dist="$1"

    case "$DIST_FILTER" in
        all) return 0 ;;
        el8 | el9 | el10) [ "$DIST_FILTER" = "$dist" ] ;;
        *) die "--dist must be el8, el9, el10, or all" ;;
    esac
}

want_target() {
    local target="$1"
    local selected

    if [ "${#ONLY_TARGETS[@]}" -eq 0 ]; then
        return 0
    fi
    for selected in "${ONLY_TARGETS[@]}"; do
        if [ "$selected" = "$target" ]; then
            return 0
        fi
    done
    return 1
}

platform_arch() {
    case "$1" in
        linux/amd64) printf 'x86_64\n' ;;
        linux/arm64) printf 'aarch64\n' ;;
        *) die "unknown Docker platform: $1" ;;
    esac
}

ensure_source_cache() {
    local missing=0
    local required

    mkdir -p "$CACHE_DIR"
    for required in \
        third-party-src.tar.gz \
        third-party-deps-src.tar.gz \
        "php-${PHP_VERSION}.tar.gz" \
        "php-litespeed-${LSAPI_VERSION}.tgz"
    do
        if [ ! -s "$CACHE_DIR/$required" ]; then
            missing=1
        fi
    done

    if [ "$missing" -eq 1 ]; then
        bash "$ROOT/scripts/create-release-source-cache.sh" "$CACHE_DIR"
    fi
}

create_ols_source_tarball() {
    local tarball="$1"
    local tmpdir repo tmp_tgz

    if [ -s "$tarball" ]; then
        tar tzf "$tarball" >/dev/null
        return 0
    fi

    tmpdir="$(mktemp -d)"
    repo="$tmpdir/openlitespeed"
    tmp_tgz="$tarball.tmp.$$"
    rm -f "$tmp_tgz"

    git config --global http.version HTTP/1.1 || true
    git config --global http.lowSpeedLimit 32768 || true
    git config --global http.lowSpeedTime 120 || true
    git clone --depth 1 --recurse-submodules --shallow-submodules \
        --branch "v${OLS_VERSION}" \
        https://github.com/litespeedtech/openlitespeed.git "$repo"
    tar czf "$tmp_tgz" \
        --exclude=.git \
        --transform="flags=r;s,^\.,openlitespeed-${OLS_VERSION}," \
        -C "$repo" .
    mv -f "$tmp_tgz" "$tarball"
    rm -rf "$tmpdir"
}

create_module_source_tarball() {
    local tarball="$1"

    tar czf "$tarball" \
        --transform="flags=r;s,^\.,litehttpd-module," \
        --exclude=.git \
        --exclude=build \
        --exclude='build-*' \
        --exclude=node_modules \
        --exclude=docs/node_modules \
        --exclude=public-repo/litehttpd/docs/node_modules \
        -C "$ROOT" .
}

build_ols_rpm() {
    local dist="$1"
    local platform="$2"
    local arch target tmpdir ols_tgz module_tgz

    arch="$(platform_arch "$platform")"
    target="${dist}-${arch}"
    want_dist "$dist" || return 0
    want_arch "$arch" || return 0
    want_target "$target" || return 0

    tmpdir="$(mktemp -d)"
    ols_tgz="$CACHE_DIR/v${OLS_VERSION}.tar.gz"
    module_tgz="$tmpdir/litehttpd-module-src.tar.gz"
    create_ols_source_tarball "$ols_tgz" || {
        rm -rf "$tmpdir"
        return 1
    }
    create_module_source_tarball "$module_tgz" || {
        rm -rf "$tmpdir"
        return 1
    }

    echo "==> Building openlitespeed-litehttpd RPM for $target"
    # shellcheck disable=SC2016
    if ! run_docker \
        --platform "$platform" \
        -v "$ROOT:/src:ro" \
        -v "$ols_tgz:/tmp/v${OLS_VERSION}.tar.gz:ro" \
        -v "$module_tgz:/tmp/litehttpd-module-src.tar.gz:ro" \
        -v "$CACHE_DIR/third-party-src.tar.gz:/tmp/third-party-src.tar.gz:ro" \
        -v "$CACHE_DIR/third-party-deps-src.tar.gz:/tmp/third-party-deps-src.tar.gz:ro" \
        -v "$CACHE_DIR/php-${PHP_VERSION}.tar.gz:/tmp/php-${PHP_VERSION}.tar.gz:ro" \
        -v "$CACHE_DIR/php-litespeed-${LSAPI_VERSION}.tgz:/tmp/php-litespeed-${LSAPI_VERSION}.tgz:ro" \
        -v "$OUT_DIR:/output" \
        -e OLS_VERSION="$OLS_VERSION" \
        -e MODULE_VERSION="$MODULE_VERSION" \
        -e PHP_VERSION="$PHP_VERSION" \
        -e LSAPI_VERSION="$LSAPI_VERSION" \
        -e DIST_TAG="$dist" \
        -e ASSET_ARCH="$arch" \
        almalinux:8 \
        bash -lc '
            set -euo pipefail

            if command -v dnf >/dev/null 2>&1; then
                dnf -y install dnf-plugins-core
                dnf versionlock add glibc glibc-devel glibc-headers glibc-common 2>/dev/null || \
                    echo "WARNING: glibc versionlock failed; binary may require newer glibc"
                dnf -y --setopt=install_weak_deps=False install \
                    rpm-build gcc gcc-c++ cmake make bison autoconf automake libtool \
                    systemd-rpm-macros which git wget clang patch perl golang binutils \
                    libcap-devel libaio-devel expat-devel libxml2-devel zlib-devel
            else
                yum -y --setopt=install_weak_deps=False install \
                    rpm-build gcc gcc-c++ cmake3 make bison autoconf automake libtool \
                    systemd-rpm-macros which git wget clang patch perl binutils \
                    libcap-devel libaio-devel expat-devel libxml2-devel zlib-devel
                yum -y --setopt=install_weak_deps=False install golang || true
                command -v cmake >/dev/null 2>&1 || ln -sf /usr/bin/cmake3 /usr/bin/cmake
            fi

            mkdir -p /root/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
            cp "/tmp/v${OLS_VERSION}.tar.gz" /root/rpmbuild/SOURCES/
            cp /src/patches/0001-lsiapi-phpconfig.patch /root/rpmbuild/SOURCES/
            cp /src/patches/0002-lsiapi-rewrite.patch /root/rpmbuild/SOURCES/
            cp /src/patches/0003-readapacheconf.patch /root/rpmbuild/SOURCES/
            cp /src/patches/0004-autoindex-403.patch /root/rpmbuild/SOURCES/
            cp /src/tools/apacheconf_parser.c /root/rpmbuild/SOURCES/
            cp /src/tools/apacheconf_parser.h /root/rpmbuild/SOURCES/
            cp /src/tools/ols_config_writer.c /root/rpmbuild/SOURCES/
            cp /src/tools/ols_config_writer.h /root/rpmbuild/SOURCES/
            cp /tmp/litehttpd-module-src.tar.gz /root/rpmbuild/SOURCES/
            cp /tmp/third-party-src.tar.gz /root/rpmbuild/SOURCES/
            cp /tmp/third-party-deps-src.tar.gz /root/rpmbuild/SOURCES/
            cp "/tmp/php-${PHP_VERSION}.tar.gz" /root/rpmbuild/SOURCES/
            cp "/tmp/php-litespeed-${LSAPI_VERSION}.tgz" /root/rpmbuild/SOURCES/

            rpmbuild -bb --noclean --with source_cache \
                --define "_topdir /root/rpmbuild" \
                --define "dist .${DIST_TAG}" \
                /src/packaging/openlitespeed-litehttpd.spec
            find /root/rpmbuild/RPMS -name "openlitespeed-litehttpd-*.rpm" ! -name "*debug*" \
                -exec cp {} /output/ \;

            LSWS="$(find /root/rpmbuild/BUILDROOT -path "*/usr/local/lsws" -type d -print -quit)"
            [ -n "$LSWS" ] || { echo "FATAL: installed LSWS tree not found"; exit 1; }
            BIN="$LSWS/bin/openlitespeed"
            ADMINPHP="$LSWS/admin/fcgi-bin/admin_php"
            LSPHP5="$LSWS/fcgi-bin/lsphp5"
            [ -x "$BIN" ] || { echo "FATAL: openlitespeed binary missing"; exit 1; }
            [ -x "$ADMINPHP" ] || { echo "FATAL: admin_php missing"; exit 1; }
            [ -x "$LSPHP5" ] || { echo "FATAL: lsphp5 missing"; exit 1; }
            P1=$(strings "$BIN" | grep -c "set_php_config_value")
            P2=$(strings "$BIN" | grep -c "parse_rewrite_rules")
            P3=$(strings "$BIN" | grep -c "readApacheConf")
            P4=$(strings "$BIN" | grep -c "indexes_disabled")
            echo "Patch verification: P1=$P1 P2=$P2 P3=$P3 P4=$P4"
            [ "$P1" -ge 1 ] && [ "$P2" -ge 1 ] && [ "$P3" -ge 1 ] && [ "$P4" -ge 1 ] || \
                { echo "FATAL: patches missing from RPM binary"; exit 1; }
            "$ADMINPHP" -v 2>&1 | grep -q "PHP 7.4.33 (litespeed)" || \
                { echo "FATAL: admin_php is not the locally built litespeed PHP"; exit 1; }
            "$LSPHP5" -v >/dev/null 2>&1 || \
                { echo "FATAL: bundled lsphp5 cannot run"; exit 1; }
            if readelf -V "$ADMINPHP" 2>/dev/null | grep -Eq "GLIBC_2\\.(29|[3-9][0-9])"; then
                echo "FATAL: admin_php requires GLIBC newer than EL8 provides"
                readelf -V "$ADMINPHP" | grep -o "GLIBC_[0-9]\\+\\.[0-9]\\+" | sort -uV
                exit 1
            fi
            [ ! -e "$LSWS/adminpasswd" ] || \
                { echo "FATAL: build-time WebAdmin password is included"; exit 1; }
            [ ! -e "$LSWS/phpbuild" ] || \
                { echo "FATAL: phpbuild source/build artifacts are included"; exit 1; }

            if [ "$DIST_TAG" = "el8" ]; then
                ST=/tmp/tarballs
                rm -rf "$ST"
                mkdir -p "$ST"
                FULL="openlitespeed-litehttpd-${OLS_VERSION}"
                PATCH="litehttpd-ols-patch-${OLS_VERSION}"
                MODP="litehttpd-module-${MODULE_VERSION}"

                mkdir -p "$ST/$FULL/lsws"
                cp -a "$LSWS/." "$ST/$FULL/lsws/"
                echo TGZ > "$ST/$FULL/lsws/PLAT"
                install -m 755 /src/packaging/tarball/install-full.sh "$ST/$FULL/install.sh"
                tar czf "/output/${FULL}-${ASSET_ARCH}-linux.tgz" -C "$ST" "$FULL"
                ! tar tzf "/output/${FULL}-${ASSET_ARCH}-linux.tgz" | grep -Eq "(^|/)adminpasswd$|(^|/)phpbuild(/|$)" || \
                    { echo "FATAL: full tgz includes build-time password or PHP build artifacts"; exit 1; }

                mkdir -p "$ST/$PATCH"
                install -m 755 "$BIN" "$ST/$PATCH/openlitespeed"
                install -m 755 "$LSWS/modules/litehttpd_htaccess.so" "$ST/$PATCH/litehttpd_htaccess.so"
                echo "$OLS_VERSION" > "$ST/$PATCH/VERSION"
                install -m 755 /src/packaging/tarball/install-patch.sh "$ST/$PATCH/install.sh"
                tar czf "/output/${PATCH}-${ASSET_ARCH}-linux.tgz" -C "$ST" "$PATCH"

                mkdir -p "$ST/$MODP"
                install -m 755 "$LSWS/modules/litehttpd_htaccess.so" "$ST/$MODP/litehttpd_htaccess.so"
                install -m 755 /src/packaging/tarball/install-module.sh "$ST/$MODP/install.sh"
                tar czf "/output/${MODP}-${ASSET_ARCH}-linux.tgz" -C "$ST" "$MODP"
            fi

            ls -lh /output/openlitespeed-litehttpd-*".${DIST_TAG}.${ASSET_ARCH}.rpm"
        '; then
        rm -rf "$tmpdir"
        return 1
    fi

    rm -rf "$tmpdir"
    fix_output_owner
    if ! find "$OUT_DIR" -maxdepth 1 -type f \
        -name "openlitespeed-litehttpd-${OLS_VERSION}-*.${dist}.${arch}.rpm" | grep -q .; then
        echo "ERROR: missing full OLS RPM for $target in $OUT_DIR" >&2
        return 1
    fi
}

OLS_SPEC="$ROOT/packaging/openlitespeed-litehttpd.spec"
MODULE_SPEC="$ROOT/packaging/litehttpd.spec"
OLS_VERSION="$(spec_global_value ols_version "$OLS_SPEC")"
MODULE_VERSION="$(spec_version "$MODULE_SPEC")"
PHP_VERSION="$(spec_global_value php_version "$OLS_SPEC")"
LSAPI_VERSION="$(spec_global_value lsapi_version "$OLS_SPEC")"
[ -n "$OLS_VERSION" ] || die "could not read OLS version"
[ -n "$MODULE_VERSION" ] || die "could not read module version"
[ -n "$PHP_VERSION" ] || die "could not read PHP version"
[ -n "$LSAPI_VERSION" ] || die "could not read LSAPI version"

mkdir -p "$OUT_DIR" "$CACHE_DIR"
ensure_source_cache
select_docker

for dist in el8 el9 el10; do
    run_step build_ols_rpm "$dist" "linux/amd64"
    run_step build_ols_rpm "$dist" "linux/arm64"
done

echo "Artifacts written to $OUT_DIR"
if [ "$FAIL_COUNT" -gt 0 ]; then
    die "$FAIL_COUNT matrix leg(s) failed"
fi
