#!/usr/bin/env bash
# Assemble patch/module tarballs from full openlitespeed-litehttpd tarballs.
# This mirrors the tarball assembly in .github/workflows/release.yml and is
# useful when a local/full RPM build produced only the full tgz.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OLS_SPEC="$ROOT/packaging/openlitespeed-litehttpd.spec"
MODULE_SPEC="$ROOT/packaging/litehttpd.spec"
OUT_DIR=""
REQUIRE=0

usage() {
    cat <<'USAGE'
Usage: bash scripts/assemble-release-tarballs.sh [--out-dir DIR] [--require] [artifact-dir ...]

Finds openlitespeed-litehttpd-<ols-version>-<arch>-linux.tgz for x86_64 and
aarch64, then assembles:
  litehttpd-ols-patch-<ols-version>-<arch>-linux.tgz
  litehttpd-module-<module-version>-<arch>-linux.tgz

If --out-dir is omitted, each derived tarball is written next to its source
full tarball. If no artifact directories are given, the script looks in:
  /tmp/ols-p2-x86-artifacts
  /tmp/ols-p2-artifacts
  output
  artifacts
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --out-dir)
            [ "$#" -ge 2 ] || {
                usage >&2
                exit 2
            }
            OUT_DIR="$2"
            shift 2
            ;;
        --require)
            REQUIRE=1
            shift
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            break
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

collect_artifact_dirs() {
    if [ "$#" -gt 0 ]; then
        printf '%s\n' "$@"
        return 0
    fi

    printf '%s\n' \
        /tmp/ols-p2-x86-artifacts \
        /tmp/ols-p2-artifacts \
        "$ROOT/output" \
        "$ROOT/artifacts"
}

find_full_tgz() {
    local arch="$1"
    local dir
    shift

    while IFS= read -r dir; do
        [ -d "$dir" ] || continue
        find "$dir" -maxdepth 1 -type f \
            -name "openlitespeed-litehttpd-${OLS_VERSION}-${arch}-linux.tgz"
    done < <(collect_artifact_dirs "$@") | sort
}

validate_tgz() {
    local tgz_file="$1"
    local base

    base="$(basename "$tgz_file")"
    tar tzf "$tgz_file" >/dev/null
    if tar tzf "$tgz_file" | grep -Eq '(^|/)adminpasswd$|(^|/)phpbuild(/|$)'; then
        die "tarball contains build-time secret or phpbuild tree: $base"
    fi
}

assemble_for_arch() {
    local arch="$1"
    local full_tgz="$2"
    local dest_dir="$3"
    local tmpdir full_dir lsws bin module patch_dir module_dir patch_tgz module_tgz

    tmpdir="$(mktemp -d)"
    tar xzf "$full_tgz" -C "$tmpdir"
    full_dir="$tmpdir/openlitespeed-litehttpd-${OLS_VERSION}"
    lsws="$full_dir/lsws"
    bin="$lsws/bin/openlitespeed"
    module="$lsws/modules/litehttpd_htaccess.so"

    [ -x "$bin" ] || die "openlitespeed binary missing in $(basename "$full_tgz")"
    [ -f "$module" ] || die "litehttpd_htaccess.so missing in $(basename "$full_tgz")"
    mkdir -p "$dest_dir"

    patch_dir="$tmpdir/litehttpd-ols-patch-${OLS_VERSION}"
    mkdir -p "$patch_dir"
    install -m 755 "$bin" "$patch_dir/openlitespeed"
    install -m 755 "$module" "$patch_dir/litehttpd_htaccess.so"
    printf '%s\n' "$OLS_VERSION" > "$patch_dir/VERSION"
    install -m 755 "$ROOT/packaging/tarball/install-patch.sh" "$patch_dir/install.sh"
    patch_tgz="$dest_dir/litehttpd-ols-patch-${OLS_VERSION}-${arch}-linux.tgz"
    tar czf "$patch_tgz" -C "$tmpdir" "$(basename "$patch_dir")"
    validate_tgz "$patch_tgz"
    echo "OK: $patch_tgz"

    module_dir="$tmpdir/litehttpd-module-${MODULE_VERSION}"
    mkdir -p "$module_dir"
    install -m 755 "$module" "$module_dir/litehttpd_htaccess.so"
    install -m 755 "$ROOT/packaging/tarball/install-module.sh" "$module_dir/install.sh"
    module_tgz="$dest_dir/litehttpd-module-${MODULE_VERSION}-${arch}-linux.tgz"
    tar czf "$module_tgz" -C "$tmpdir" "$(basename "$module_dir")"
    validate_tgz "$module_tgz"
    echo "OK: $module_tgz"

    rm -rf "$tmpdir"
}

command -v tar >/dev/null 2>&1 || die "missing required tool: tar"

OLS_VERSION="$(spec_global_value ols_version "$OLS_SPEC")"
MODULE_VERSION="$(spec_version "$MODULE_SPEC")"
[ -n "$OLS_VERSION" ] || die "could not read OLS version from $OLS_SPEC"
[ -n "$MODULE_VERSION" ] || die "could not read module version from $MODULE_SPEC"

for arch in x86_64 aarch64; do
    dest=""
    mapfile -t matches < <(find_full_tgz "$arch" "$@")
    if [ "${#matches[@]}" -eq 0 ]; then
        if [ "$REQUIRE" = "1" ]; then
            die "missing full tarball for $arch"
        fi
        echo "SKIP: missing full tarball for $arch"
        continue
    fi
    if [ "${#matches[@]}" -gt 1 ]; then
        die "duplicate full tarballs for $arch: ${matches[*]}"
    fi

    if [ -n "$OUT_DIR" ]; then
        dest="$OUT_DIR"
    else
        dest="$(dirname "${matches[0]}")"
    fi
    assemble_for_arch "$arch" "${matches[0]}" "$dest"
done
