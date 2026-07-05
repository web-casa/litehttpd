#!/usr/bin/env bash
# Validate the release workflow matrix and release-package safety checks.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RELEASE_YML="$ROOT/.github/workflows/release.yml"
FAIL=0

fail() {
    echo "ERROR: $*" >&2
    FAIL=1
}

require_literal() {
    local literal="$1"
    local label="$2"

    if grep -Fq -- "$literal" "$RELEASE_YML"; then
        echo "OK: $label"
    else
        fail "$label missing ($literal)"
    fi
}

require_regex() {
    local regex="$1"
    local label="$2"

    if grep -Eq "$regex" "$RELEASE_YML"; then
        echo "OK: $label"
    else
        fail "$label missing ($regex)"
    fi
}

count_literal() {
    local literal="$1"
    local count

    count="$(grep -F -c -- "$literal" "$RELEASE_YML" || true)"
    printf '%s\n' "${count:-0}"
}

require_count_at_least() {
    local literal="$1"
    local minimum="$2"
    local label="$3"
    local count

    count="$(count_literal "$literal")"
    if [ "$count" -ge "$minimum" ]; then
        echo "OK: $label ($count >= $minimum)"
    else
        fail "$label count too low: $count < $minimum ($literal)"
    fi
}

echo "========================================"
echo " Release Matrix Check"
echo "========================================"

for suffix in \
    el8-x86_64 el9-x86_64 el10-x86_64 ubuntu22-x86_64 \
    el8-aarch64 el9-aarch64 el10-aarch64 ubuntu22-aarch64
do
    require_literal "suffix: $suffix" "module .so matrix includes $suffix"
done

for arch in x86_64 aarch64; do
    require_literal "arch_suffix: $arch" "module RPM matrix includes $arch"
    require_literal "asset_arch: $arch" "OLS RPM matrix includes $arch"
done

for dist in el8 el9 el10; do
    require_count_at_least "dist_tag: $dist" 2 "OLS RPM matrix includes $dist for both arches"
    require_literal "repo/\$dist/\$arch/RPMS" "RPM repo loop publishes $dist per arch"
done

require_count_at_least "platform: linux/arm64" 4 "ARM64 release jobs"
require_count_at_least "qemu: true" 4 "QEMU-enabled release jobs"
require_literal "docker/setup-qemu-action@v3" "QEMU setup action"
require_literal "fail-fast: false" "OLS RPM matrix fail-fast disabled"
require_literal "timeout-minutes: 120" "OLS RPM release timeout"

require_literal "bash tests/check_patches.sh" "release workflow runs patch consistency checks"
require_literal "bash scripts/check-release-source-isolated.sh" "release workflow checks clean release source"
require_literal "bash scripts/check-release-metadata.sh" "release workflow checks release metadata"
require_literal "bash scripts/check-apps-matrix-budget-policy.sh" "release workflow checks budget policy"
require_literal "bash scripts/check-public-release-sync.sh" "release workflow reports public sync delta"
require_literal "-DBUILD_TESTING=OFF" "module .so release build skips test dependency fetch"
require_count_at_least "pcre2-devel" 7 "EL release module builds install PCRE2 development files"
require_count_at_least "libpcre2-dev" 2 "Ubuntu release module builds install PCRE2 development files"
require_literal "bash scripts/create-release-source-cache.sh /tmp" "release workflow creates source cache"
require_literal "--with source_cache" "OLS rpmbuild uses source cache"
require_literal "/tmp/third-party-src.tar.gz:/tmp/third-party-src.tar.gz:ro" "third-party source cache is mounted"
require_literal "/tmp/third-party-deps-src.tar.gz:/tmp/third-party-deps-src.tar.gz:ro" "third-party dependency cache is mounted"
require_literal "/tmp/php-7.4.33.tar.gz:/tmp/php-7.4.33.tar.gz:ro" "PHP source cache is mounted"
require_literal "/tmp/php-litespeed-8.1.tgz:/tmp/php-litespeed-8.1.tgz:ro" "LSAPI source cache is mounted"

require_literal "LSWS/adminpasswd" "release workflow checks build-time WebAdmin password"
require_literal "LSWS/phpbuild" "release workflow checks phpbuild removal"
require_literal "GLIBC_2\\\\.(29|[3-9][0-9])" "release workflow checks admin_php GLIBC ceiling"
require_literal "PHP 7.4.33 (litespeed)" "release workflow checks local admin_php"
# shellcheck disable=SC2016
require_literal '[ "$P4" -ge 1 ]' "release workflow checks autoindex patch marker"
# shellcheck disable=SC2016
require_literal '${ASSET_ARCH}-linux.tgz' "tarball names include asset architecture"
require_regex 'grep -Eq "\(\^\|/\)adminpasswd\$|\(\^\|/\)phpbuild\(/\|\$\)"' "full tgz secret/build artifact check"

require_literal "repo_gpgcheck=1" "setup repo enables repo metadata GPG verification"
require_literal "for dist in el8 el9 el10" "publish repo loops over EL versions"
require_literal "for arch in x86_64 aarch64" "publish repo loops over architectures"
require_literal "createrepo_c \"repo/\$dist/\$arch/\"" "publish repo generates repodata per arch"
require_literal "repodata/repomd.xml" "publish repo signs repomd.xml"
require_literal "find artifacts -maxdepth 1 -name '*.tgz'" "publish repo copies tarballs without unsafe glob"
require_literal "REPO_REQUIRE_FULL_MATRIX=1 bash scripts/check-rpm-repo-dry-run.sh artifacts" \
    "publish repo validates full RPM matrix before signing/uploading"
require_literal "set -euo pipefail" "publish repo signing step fails closed"
# shellcheck disable=SC2016
require_literal 'rpmsign --addsign "$rpm_file"' "publish repo does not hide rpmsign failures behind a pipeline"
require_literal "needs: [build-module, build-rpm-module, build-rpm-ols, publish-rpm-repo, test]" \
    "final release waits for RPM repo publish"
require_literal "RELEASE_REQUIRE_FULL_ARTIFACTS=1 bash scripts/check-release-artifacts.sh rel" \
    "final release validates complete artifact matrix before publishing"
require_count_at_least "uploaded=0" 5 "release uploads fail closed after retry exhaustion"

echo "========================================"
exit "$FAIL"
