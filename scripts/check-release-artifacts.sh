#!/usr/bin/env bash
# Validate release artifact names and metadata against the release workflow
# matrix. Default mode reports missing matrix slots; --require-full makes any
# missing slot a release blocker.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REQUIRE_FULL="${RELEASE_REQUIRE_FULL_ARTIFACTS:-0}"
OLS_SPEC="$ROOT/packaging/openlitespeed-litehttpd.spec"
MODULE_SPEC="$ROOT/packaging/litehttpd.spec"

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-release-artifacts.sh [--require-full] [artifact-dir ...]

If no artifact directories are given, the script looks in:
  /tmp/ols-p2-x86-artifacts
  /tmp/ols-p2-artifacts
  output
  artifacts

Environment:
  RELEASE_REQUIRE_FULL_ARTIFACTS=1  Fail if any expected matrix slot is missing.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --require-full)
            REQUIRE_FULL=1
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

have_tool() {
    command -v "$1" >/dev/null 2>&1
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
        find "$artifact_dir" -maxdepth 1 -type f \( -name '*.so' -o -name '*.rpm' -o -name '*.tgz' \)
    done < <(collect_artifact_dirs "$@")
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

count_matches() {
    local pattern="$1"

    find "$ARTIFACT_DIR" -maxdepth 1 -type f -name "$pattern" | wc -l | tr -d ' '
}

first_match() {
    local pattern="$1"

    find "$ARTIFACT_DIR" -maxdepth 1 -type f -name "$pattern" | sort | head -1
}

mark_present_or_missing() {
    local pattern="$1"
    local label="$2"
    local count

    count="$(count_matches "$pattern")"
    if [ "$count" -eq 1 ]; then
        echo "OK: $label"
        return 0
    fi
    if [ "$count" -gt 1 ]; then
        die "duplicate artifacts for $label ($pattern): $count"
    fi

    echo "MISSING: $label"
    MISSING_COUNT=$((MISSING_COUNT + 1))
    return 0
}

validate_rpm_metadata() {
    local rpm_file="$1"
    local expected_name="$2"
    local expected_version="$3"
    local expected_dist="$4"
    local expected_arch="$5"
    local base rpm_metadata rpm_name rpm_version rpm_release rpm_arch

    base="$(basename "$rpm_file")"
    rpm_metadata="$(rpm -qp --queryformat '%{NAME} %{VERSION} %{RELEASE} %{ARCH}\n' "$rpm_file")" ||
        die "cannot read RPM metadata: $base"
    read -r rpm_name rpm_version rpm_release rpm_arch <<< "$rpm_metadata"

    [ "$rpm_name" = "$expected_name" ] ||
        die "RPM name mismatch: $base has $rpm_name, expected $expected_name"
    [ "$rpm_version" = "$expected_version" ] ||
        die "RPM version mismatch: $base has $rpm_version, expected $expected_version"
    [ "$rpm_arch" = "$expected_arch" ] ||
        die "RPM arch mismatch: $base has $rpm_arch, expected $expected_arch"
    case "$rpm_release" in
        *".$expected_dist"*) ;;
        *) die "RPM release missing .$expected_dist: $base has $rpm_release" ;;
    esac

    if rpm -qpl "$rpm_file" | grep -Eq '(/adminpasswd$|/phpbuild(/|$))'; then
        die "RPM contains build-time secret or phpbuild tree: $base"
    fi
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

need_tool rpm
need_tool tar

expected_ols_version="$(spec_global_value ols_version "$OLS_SPEC")"
expected_module_version="$(spec_version "$MODULE_SPEC")"
[ -n "$expected_ols_version" ] || die "could not read OLS version from $OLS_SPEC"
[ -n "$expected_module_version" ] || die "could not read module version from $MODULE_SPEC"

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
ARTIFACT_DIR="$tmpdir/artifacts"
mkdir -p "$ARTIFACT_DIR"
artifact_list="$tmpdir/artifact-files.txt"
duplicate_list="$tmpdir/duplicate-artifacts.txt"

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
    die "duplicate artifact names would make release validation ambiguous"
fi
while IFS= read -r artifact_file; do
    [ -n "$artifact_file" ] || continue
    cp "$artifact_file" "$ARTIFACT_DIR/"
done < "$artifact_list"

MISSING_COUNT=0

echo "========================================"
echo " Release Artifact Matrix Check"
echo "========================================"
echo "Artifacts:  $ARTIFACT_DIR"
echo "Expected:   OpenLiteSpeed $expected_ols_version, litehttpd $expected_module_version"
echo "Strict:     $REQUIRE_FULL"

echo "--- Module .so matrix ---"
for platform in el8 el9 el10 ubuntu22; do
    for arch in x86_64 aarch64; do
        mark_present_or_missing "litehttpd_htaccess-${platform}-${arch}.so" \
            "litehttpd_htaccess ${platform}/${arch}"
    done
done

echo "--- OLS RPM matrix ---"
for dist in el8 el9 el10; do
    for arch in x86_64 aarch64; do
        pattern="openlitespeed-litehttpd-${expected_ols_version}-*.${dist}.${arch}.rpm"
        mark_present_or_missing "$pattern" "openlitespeed-litehttpd RPM ${dist}/${arch}"
        rpm_file="$(first_match "$pattern")"
        [ -n "$rpm_file" ] || continue
        validate_rpm_metadata "$rpm_file" openlitespeed-litehttpd "$expected_ols_version" "$dist" "$arch"
    done
done

echo "--- Module RPM matrix ---"
for arch in x86_64 aarch64; do
    pattern="litehttpd-${expected_module_version}-*.el9.${arch}.rpm"
    mark_present_or_missing "$pattern" "litehttpd module RPM el9/${arch}"
    rpm_file="$(first_match "$pattern")"
    [ -n "$rpm_file" ] || continue
    validate_rpm_metadata "$rpm_file" litehttpd "$expected_module_version" el9 "$arch"
done

echo "--- Binary tarball matrix ---"
for arch in x86_64 aarch64; do
    for name in \
        "openlitespeed-litehttpd-${expected_ols_version}-${arch}-linux.tgz" \
        "litehttpd-ols-patch-${expected_ols_version}-${arch}-linux.tgz" \
        "litehttpd-module-${expected_module_version}-${arch}-linux.tgz"
    do
        mark_present_or_missing "$name" "$name"
        tgz_file="$(first_match "$name")"
        [ -n "$tgz_file" ] || continue
        validate_tgz "$tgz_file"
    done
done

while IFS= read -r unexpected; do
    [ -n "$unexpected" ] || continue
    die "unexpected tarball artifact name: $(basename "$unexpected")"
done < <(
    find "$ARTIFACT_DIR" -maxdepth 1 -type f -name '*.tgz' |
        grep -Ev "/(openlitespeed-litehttpd-${expected_ols_version}|litehttpd-ols-patch-${expected_ols_version}|litehttpd-module-${expected_module_version})-(x86_64|aarch64)-linux[.]tgz$" || true
)

echo "Missing matrix slots: $MISSING_COUNT"
if [ "$REQUIRE_FULL" = "1" ] && [ "$MISSING_COUNT" -gt 0 ]; then
    die "release artifact matrix is incomplete"
fi

echo "========================================"
