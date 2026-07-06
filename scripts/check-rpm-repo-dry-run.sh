#!/usr/bin/env bash
# Build a local RPM repository from release artifacts without uploading to R2.
# This validates the repo layout, repodata generation, root tarball publishing,
# static indexes, and setup script syntax.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${RPM_REPO_DRY_RUN_DIR:-/tmp/litehttpd-rpm-repo-dry-run}"
REQUIRE_FULL_MATRIX="${REPO_REQUIRE_FULL_MATRIX:-0}"
OLS_SPEC="$ROOT/packaging/openlitespeed-litehttpd.spec"
MODULE_SPEC="$ROOT/packaging/litehttpd.spec"

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-rpm-repo-dry-run.sh [artifact-dir ...]

If no artifact directories are given, the script looks in:
  /tmp/ols-p2-x86-artifacts
  /tmp/ols-p2-artifacts
  output
  artifacts

Environment:
  RPM_REPO_DRY_RUN_DIR=<path>  Output dir. Default: /tmp/litehttpd-rpm-repo-dry-run
  REPO_REQUIRE_FULL_MATRIX=1   Require RPMs for every published EL/arch slot.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -h | --help)
            usage
            exit 0
            ;;
        *)
            break
            ;;
    esac
done

have_tool() {
    command -v "$1" >/dev/null 2>&1
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

handle_unexpected_artifact() {
    if [ "$REQUIRE_FULL_MATRIX" = "1" ]; then
        die "$*"
    fi
    echo "WARN: $*" >&2
}

need_tool() {
    have_tool "$1" || die "missing required tool: $1"
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

collect_artifact_files() {
    while IFS= read -r artifact_dir; do
        [ -d "$artifact_dir" ] || continue
        find "$artifact_dir" -maxdepth 1 -type f \( -name '*.rpm' -o -name '*.tgz' \)
    done < <(collect_artifact_dirs "$@")
}

copy_matching() {
    local src_dir="$1"
    local pattern="$2"
    local dest_dir="$3"

    find "$src_dir" -maxdepth 1 -type f -name "$pattern" -exec cp {} "$dest_dir/" \;
}

rpm_dist() {
    local name="$1"

    case "$name" in
        *.el8.*.rpm) printf 'el8\n' ;;
        *.el9.*.rpm) printf 'el9\n' ;;
        *.el10.*.rpm) printf 'el10\n' ;;
        *) printf 'unknown\n' ;;
    esac
}

rpm_arch_from_name() {
    local name="$1"

    case "$name" in
        *.x86_64.rpm) printf 'x86_64\n' ;;
        *) printf 'unknown\n' ;;
    esac
}

tgz_arch_from_name() {
    local name="$1"

    case "$name" in
        *-x86_64-linux.tgz) printf 'x86_64\n' ;;
        *) printf 'unknown\n' ;;
    esac
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

need_tool rpm
need_tool tar
need_tool createrepo_c

bash -n "$ROOT/packaging/setup-repo.sh"
bash -n "$ROOT/packaging/generate-index.sh"

expected_ols_version="$(spec_global_value ols_version "$OLS_SPEC")"
expected_module_version="$(spec_version "$MODULE_SPEC")"
[ -n "$expected_ols_version" ] || die "could not read OLS version from $OLS_SPEC"
[ -n "$expected_module_version" ] || die "could not read module version from $MODULE_SPEC"
RELEASE_ARCHES=(x86_64)

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/artifacts" "$OUT_DIR/repo-staging" "$OUT_DIR/repo"
artifact_list="$OUT_DIR/artifact-files.txt"
duplicate_list="$OUT_DIR/duplicate-artifacts.txt"

collect_artifact_files "$@" | sort > "$artifact_list"
while IFS= read -r artifact_file; do
    [ -n "$artifact_file" ] || continue
    basename "$artifact_file"
done < "$artifact_list" | sort | uniq -d > "$duplicate_list"
if [ -s "$duplicate_list" ]; then
    echo "Duplicate artifact names across input directories:" >&2
    while IFS= read -r duplicate; do
        [ -n "$duplicate" ] || continue
        echo "  $duplicate" >&2
        grep -F "/$duplicate" "$artifact_list" | sed 's/^/    /' >&2
    done < "$duplicate_list"
    die "duplicate artifact names would make repo staging ambiguous"
fi
while IFS= read -r artifact_file; do
    [ -n "$artifact_file" ] || continue
    cp "$artifact_file" "$OUT_DIR/artifacts/"
done < "$artifact_list"

find "$OUT_DIR/artifacts" -maxdepth 1 -type f -name '*.rpm' -exec cp {} "$OUT_DIR/repo-staging/" \;

rpm_count="$(find "$OUT_DIR/repo-staging" -maxdepth 1 -type f -name '*.rpm' | wc -l | tr -d ' ')"
tgz_count="$(find "$OUT_DIR/artifacts" -maxdepth 1 -type f -name '*.tgz' | wc -l | tr -d ' ')"

[ "$rpm_count" -gt 0 ] || die "no RPM artifacts found"

echo "========================================"
echo " RPM Repo Dry Run"
echo "========================================"
echo "Artifacts: $OUT_DIR/artifacts"
echo "RPMs:      $rpm_count"
echo "Tarballs:  $tgz_count"
echo "Expected:  openlitespeed-litehttpd $expected_ols_version, litehttpd $expected_module_version"

while IFS= read -r rpm_file; do
    base="$(basename "$rpm_file")"
    rpm_metadata="$(rpm -qp --queryformat '%{NAME} %{VERSION} %{RELEASE} %{ARCH}\n' "$rpm_file")" ||
        die "cannot read RPM metadata: $base"
    read -r rpm_name rpm_version rpm_release rpm_arch <<< "$rpm_metadata"

    case "$base" in
        *debug*)
            handle_unexpected_artifact "debug RPM must not be published: $base"
            continue
            ;;
        openlitespeed-litehttpd-*.rpm | litehttpd-*.rpm) ;;
        *)
            handle_unexpected_artifact "unexpected RPM artifact name: $base"
            continue
            ;;
    esac
    case "$rpm_name" in
        openlitespeed-litehttpd)
            [ "$rpm_version" = "$expected_ols_version" ] ||
                die "openlitespeed-litehttpd version mismatch: $base has $rpm_version, expected $expected_ols_version"
            ;;
        litehttpd)
            [ "$rpm_version" = "$expected_module_version" ] ||
                die "litehttpd version mismatch: $base has $rpm_version, expected $expected_module_version"
            [ "$(rpm_dist "$base")" = "el9" ] ||
                die "standalone litehttpd module RPM is only expected for el9: $base"
            ;;
        *)
            die "unexpected RPM package name in metadata: $base has $rpm_name"
            ;;
    esac

    dist="$(rpm_dist "$base")"
    arch="$(rpm_arch_from_name "$base")"
    if [ "$dist" = "unknown" ]; then
        handle_unexpected_artifact "RPM missing EL dist tag: $base"
        continue
    fi
    if [ "$arch" = "unknown" ]; then
        handle_unexpected_artifact "RPM missing supported architecture: $base"
        continue
    fi
    [ "$rpm_arch" = "$arch" ] ||
        die "RPM metadata arch mismatch: $base filename says $arch, metadata says $rpm_arch"
    case "$rpm_release" in
        *".$dist"*) ;;
        *) die "RPM metadata release missing .$dist: $base has $rpm_release" ;;
    esac

    rpm -qp "$rpm_file" >/dev/null
    if rpm -qpl "$rpm_file" | grep -Eq '(/adminpasswd$|/phpbuild(/|$))'; then
        die "RPM contains build-time secret or phpbuild tree: $base"
    fi
    rpm -qp --queryformat 'OK: %{NAME} %{VERSION}-%{RELEASE} %{ARCH}\n' "$rpm_file"
done < <(find "$OUT_DIR/repo-staging" -maxdepth 1 -type f -name '*.rpm' | sort)

while IFS= read -r tgz_file; do
    base="$(basename "$tgz_file")"
    arch="$(tgz_arch_from_name "$base")"
    if [ "$arch" = "unknown" ]; then
        handle_unexpected_artifact "tarball missing supported architecture: $base"
        continue
    fi
    case "$base" in
        "openlitespeed-litehttpd-${expected_ols_version}-${arch}-linux.tgz" | \
        "litehttpd-ols-patch-${expected_ols_version}-${arch}-linux.tgz" | \
        "litehttpd-module-${expected_module_version}-${arch}-linux.tgz")
            ;;
        *)
            handle_unexpected_artifact "unexpected tarball artifact name or version: $base"
            continue
            ;;
    esac

    tar tzf "$tgz_file" >/dev/null
    if tar tzf "$tgz_file" | grep -Eq '(^|/)adminpasswd$|(^|/)phpbuild(/|$)'; then
        die "tarball contains build-time secret or phpbuild tree: $base"
    fi
    echo "OK: tarball $base ($arch)"
done < <(find "$OUT_DIR/artifacts" -maxdepth 1 -type f -name '*.tgz' | sort)

for dist in el8 el9 el10; do
    for arch in "${RELEASE_ARCHES[@]}"; do
        repo_dir="$OUT_DIR/repo/$dist/$arch"
        rpm_dir="$repo_dir/RPMS"
        mkdir -p "$rpm_dir"
        copy_matching "$OUT_DIR/repo-staging" "openlitespeed-litehttpd-*.$dist.$arch.rpm" "$rpm_dir"
        if [ "$dist" = "el9" ]; then
            copy_matching "$OUT_DIR/repo-staging" "litehttpd-*.$dist.$arch.rpm" "$rpm_dir"
        fi

        ols_slot_count="$(find "$rpm_dir" -maxdepth 1 -type f -name "openlitespeed-litehttpd-*.$dist.$arch.rpm" | wc -l | tr -d ' ')"
        module_slot_count="$(find "$rpm_dir" -maxdepth 1 -type f -name "litehttpd-*.$dist.$arch.rpm" | wc -l | tr -d ' ')"
        slot_count=$((ols_slot_count + module_slot_count))
        if [ "$REQUIRE_FULL_MATRIX" = "1" ]; then
            expected_module_count=0
            if [ "$dist" = "el9" ]; then
                expected_module_count=1
            fi
            if [ "$ols_slot_count" -ne 1 ]; then
                die "RPM matrix mismatch for $dist/$arch: got $ols_slot_count full OLS RPMs, expected 1"
            fi
            if [ "$module_slot_count" -ne "$expected_module_count" ]; then
                die "RPM matrix mismatch for $dist/$arch: got $module_slot_count module RPMs, expected $expected_module_count"
            fi
        fi

        createrepo_c "$repo_dir" >/dev/null
        test -f "$repo_dir/repodata/repomd.xml"
        echo "OK: repo/$dist/$arch ($slot_count RPMs)"
    done
done

[ -s "$ROOT/packaging/RPM-GPG-KEY-litehttpd" ] || die "missing or empty RPM GPG public key"
cp "$ROOT/packaging/RPM-GPG-KEY-litehttpd" "$OUT_DIR/repo/"
cp "$ROOT/packaging/setup-repo.sh" "$OUT_DIR/repo/setup.sh"
find "$OUT_DIR/artifacts" -maxdepth 1 -type f -name '*.tgz' -exec cp {} "$OUT_DIR/repo/" \;

bash "$ROOT/packaging/generate-index.sh" "$OUT_DIR/repo" >/dev/null
test -f "$OUT_DIR/repo/index.html"
test -f "$OUT_DIR/repo/el8/x86_64/index.html"

grep -q 'repo_gpgcheck=1' "$OUT_DIR/repo/setup.sh"
# shellcheck disable=SC2016
grep -q 'baseurl=${REPO_URL}/el${MAJOR}/${ARCH}/' "$OUT_DIR/repo/setup.sh"
grep -q 'supported: x86_64' "$OUT_DIR/repo/setup.sh"

echo "OK: setup.sh syntax and arch-aware baseurl"
echo "OK: static indexes generated"
echo "Dry-run repo: $OUT_DIR/repo"
echo "========================================"
