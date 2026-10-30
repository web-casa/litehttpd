#!/usr/bin/env bash
# Build release .so and standalone module RPM artifacts with the same container
# matrix used by .github/workflows/release.yml.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${RELEASE_ARTIFACT_OUT_DIR:-/tmp/ols-p2-artifacts}"
BUILD_SO=1
BUILD_RPM=1
ARCH_FILTER="x86_64"
CONTINUE_ON_ERROR="${RELEASE_ARTIFACT_CONTINUE_ON_ERROR:-0}"
FAIL_COUNT=0
DOCKER_CMD=()
ONLY_TARGETS=()

usage() {
    cat <<'USAGE'
Usage: bash scripts/build-release-module-artifacts.sh [--so-only|--rpm-only] [--arch x86_64] [--out-dir DIR] [--only TARGET] [--continue-on-error]

Builds:
  litehttpd_htaccess-{el8,el9,el10,ubuntu22}-x86_64.so
  litehttpd-<version>-*.el9.x86_64.rpm

Targets accepted by --only:
  el8-x86_64 el9-x86_64 el10-x86_64 ubuntu22-x86_64
  rpm-x86_64

The Docker build commands intentionally mirror the Release workflow matrix.
If the current user cannot access Docker directly, the script tries
`sudo -n docker`.

Environment:
  RELEASE_ARTIFACT_CONTINUE_ON_ERROR=1  Continue remaining matrix legs after a
                                        local Docker/emulation failure.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --so-only)
            BUILD_SO=1
            BUILD_RPM=0
            shift
            ;;
        --rpm-only)
            BUILD_SO=0
            BUILD_RPM=1
            shift
            ;;
        --arch)
            [ "$#" -ge 2 ] || {
                usage >&2
                exit 2
            }
            ARCH_FILTER="$2"
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
        --only)
            [ "$#" -ge 2 ] || {
                usage >&2
                exit 2
            }
            ONLY_TARGETS+=("$2")
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

spec_version() {
    awk '$1 == "Version:" { print $2; exit }' "$ROOT/packaging/litehttpd.spec"
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
        x86_64) [ "$ARCH_FILTER" = "$arch" ] ;;
        *) die "--arch must be x86_64" ;;
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
        *) die "unknown Docker platform: $1" ;;
    esac
}

build_so() {
    local container="$1"
    local suffix="$2"
    local platform="$3"
    local pkg_cmd="$4"
    local arch out_file

    arch="$(platform_arch "$platform")"
    want_arch "$arch" || return 0
    want_target "$suffix" || return 0
    out_file="$OUT_DIR/litehttpd_htaccess-${suffix}.so"

    echo "==> Building $out_file in $container ($platform)"
    # The single-quoted script runs inside the container; host variables are
    # passed as environment or deliberately spliced before execution.
    # shellcheck disable=SC2016
    if ! run_docker \
        --platform "$platform" \
        -v "$ROOT:/src:ro" \
        -v "$OUT_DIR:/output" \
        -e PKG_CMD="$pkg_cmd" \
        "$container" \
        bash -lc '
            set -euo pipefail
            eval "$PKG_CMD"
            mkdir -p /tmp/build
            cd /tmp/build
            tar cf - -C /src --exclude=build --exclude=build-* --exclude=.git . | tar xf -
            cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_C_FLAGS="-O2 -DNDEBUG"
            cmake --build build --target litehttpd_htaccess -j"$(nproc)"
            strip build/litehttpd_htaccess.so
            cp build/litehttpd_htaccess.so "/output/litehttpd_htaccess-'"$suffix"'.so"
        '; then
        return 1
    fi
    fix_output_owner
    if [ ! -s "$out_file" ]; then
        echo "ERROR: missing output: $out_file" >&2
        return 1
    fi
    file "$out_file"
}

create_module_source_tarball() {
    local version="$1"
    local tarball="$2"

    tar czf "$tarball" \
        --transform="s,^\.,litehttpd-${version}," \
        --exclude=.git \
        --exclude=build \
        --exclude='build-*' \
        --exclude=node_modules \
        --exclude=docs/node_modules \
        --exclude=public-repo/litehttpd/docs/node_modules \
        -C "$ROOT" .
}

build_module_rpm() {
    local platform="$1"
    local arch version tmpdir source_tgz

    arch="$(platform_arch "$platform")"
    want_arch "$arch" || return 0
    want_target "rpm-$arch" || return 0
    version="$(spec_version)"
    [ -n "$version" ] || die "could not read module version"

    tmpdir="$(mktemp -d)"
    source_tgz="$tmpdir/litehttpd-${version}.tar.gz"
    create_module_source_tarball "$version" "$source_tgz" || {
        rm -rf "$tmpdir"
        return 1
    }

    echo "==> Building litehttpd module RPM for $arch"
    # shellcheck disable=SC2016
    if ! run_docker \
        --platform "$platform" \
        -v "$ROOT:/src:ro" \
        -v "$source_tgz:/tmp/litehttpd-${version}.tar.gz:ro" \
        -v "$OUT_DIR:/output" \
        -e VERSION="$version" \
        almalinux:9 \
        bash -lc '
            set -euo pipefail
            dnf -y install rpm-build cmake gcc gcc-c++ git-core libxcrypt-devel pcre2-devel
            mkdir -p /root/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
            cp "/tmp/litehttpd-${VERSION}.tar.gz" /root/rpmbuild/SOURCES/
            rpmbuild -bb --define "_topdir /root/rpmbuild" /src/packaging/litehttpd.spec
            find /root/rpmbuild/RPMS -name "litehttpd-*.rpm" ! -name "*debug*" -exec cp {} /output/ \;
            ls -lh /output/litehttpd-*.rpm
        '; then
        rm -rf "$tmpdir"
        return 1
    fi
    rm -rf "$tmpdir"
    fix_output_owner
    if ! find "$OUT_DIR" -maxdepth 1 -type f -name "litehttpd-${version}-*.el9.${arch}.rpm" | grep -q .; then
        echo "ERROR: missing module RPM for $arch in $OUT_DIR" >&2
        return 1
    fi
}

mkdir -p "$OUT_DIR"
select_docker

if [ "$BUILD_SO" = "1" ]; then
    run_step build_so "almalinux:8" "el8-x86_64" "linux/amd64" \
        "yum -y install cmake3 gcc gcc-c++ git libxcrypt-devel pcre2-devel && (command -v cmake >/dev/null 2>&1 || ln -sf /usr/bin/cmake3 /usr/bin/cmake)"
    run_step build_so "almalinux:9" "el9-x86_64" "linux/amd64" \
        "dnf -y install cmake gcc gcc-c++ git-core libxcrypt-devel pcre2-devel"
    run_step build_so "almalinux:10" "el10-x86_64" "linux/amd64" \
        "dnf -y install cmake gcc gcc-c++ git-core libxcrypt-devel pcre2-devel"
    run_step build_so "ubuntu:22.04" "ubuntu22-x86_64" "linux/amd64" \
        "apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y cmake gcc g++ git libcrypt-dev libpcre2-dev"
fi

if [ "$BUILD_RPM" = "1" ]; then
    run_step build_module_rpm "linux/amd64"
fi

echo "Artifacts written to $OUT_DIR"
if [ "$FAIL_COUNT" -gt 0 ]; then
    die "$FAIL_COUNT matrix leg(s) failed"
fi
