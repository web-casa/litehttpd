#!/usr/bin/env bash
# =============================================================================
# Patch and release-target consistency checker
#
# This catches mistakes that a compiled-binary strings check cannot catch, such
# as a patched symbol being present while the added control flow is broken.
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TARGET_OLS_VERSION="1.9.1"
FAIL=0

fail() {
    echo "ERROR: $*" >&2
    FAIL=1
}

echo "========================================"
echo " Patch Consistency Check"
echo "========================================"

SPEC="$ROOT/packaging/openlitespeed-litehttpd.spec"
RELEASE_YML="$ROOT/.github/workflows/release.yml"
REWRITE_PATCH="$ROOT/patches/0002-lsiapi-rewrite.patch"
ASSET_ARCH_TGZ_LITERAL="\${ASSET_ARCH}-linux.tgz"
HAS_RELEASE_PACKAGING=0
if [ -f "$SPEC" ] &&
    [ -f "$RELEASE_YML" ] &&
    [ -f "$ROOT/packaging/setup-repo.sh" ]; then
    HAS_RELEASE_PACKAGING=1
fi

existing_files() {
    local path

    for path in "$@"; do
        [ -f "$path" ] && printf '%s\n' "$path"
    done
}

if [ "$HAS_RELEASE_PACKAGING" = "1" ]; then
    spec_version="$(awk '/^%global[[:space:]]+ols_version[[:space:]]+/ { print $3; exit }' "$SPEC")"
    if [ "$spec_version" != "$TARGET_OLS_VERSION" ]; then
        fail "spec pins OLS $spec_version, expected $TARGET_OLS_VERSION"
    else
        echo "OK: spec pins OLS $TARGET_OLS_VERSION"
    fi
else
    echo "SKIP: release packaging checks unavailable in this checkout"
fi

mapfile -t build_critical_files < <(existing_files \
    "$RELEASE_YML" \
    "$ROOT/tests/e2e/Dockerfile.custom-ols" \
    "$ROOT/tests/e2e/compare/Dockerfile.ols-module" \
    "$ROOT/tests/e2e/vps-5engine/deploy.sh" \
    "$ROOT/integration-tests/Dockerfile.ols" \
    "$ROOT/patches/README.md")

if [ "${#build_critical_files[@]}" -gt 0 ] &&
    grep -Eq 'v1\.8\.5|openlitespeed-1\.8\.5|/tmp/v1\.8\.5\.tar\.gz|libpcre3-dev' \
        "${build_critical_files[@]}"; then
    fail "build-critical files still contain stale OLS 1.8.5 or system PCRE1 references"
elif [ "$HAS_RELEASE_PACKAGING" = "1" ] && awk '
    /^%changelog/ { in_changelog = 1 }
    !in_changelog && /v1\.8\.5|openlitespeed-1\.8\.5|\/tmp\/v1\.8\.5\.tar\.gz|libpcre3-dev/ { bad = 1 }
    END { exit bad ? 0 : 1 }
' "$SPEC"; then
    fail "active spec sections still contain stale OLS 1.8.5 or system PCRE1 references"
else
    echo "OK: build-critical files no longer reference OLS 1.8.5/system PCRE1"
fi

if [ "$HAS_RELEASE_PACKAGING" = "1" ]; then
    if grep -q 'platform: linux/arm64' "$RELEASE_YML" ||
    grep -q 'asset_arch: aarch64' "$RELEASE_YML" ||
    grep -q 'aarch64' "$ROOT/packaging/setup-repo.sh"; then
        fail "release workflow or repo setup still contains aarch64 publishing support"
    elif ! grep -Fq "$ASSET_ARCH_TGZ_LITERAL" "$RELEASE_YML"; then
        fail "release workflow no longer names tarballs from the selected asset architecture"
    else
        echo "OK: release workflow publishes x86_64-only artifacts"
    fi

    if ! grep -Fq 'rm -f %{buildroot}%{install_dir}/adminpasswd' "$SPEC" ||
    ! grep -q 'build-time WebAdmin password' "$SPEC" ||
    ! grep -q 'LSWS/adminpasswd' "$RELEASE_YML" ||
    ! grep -q 'LSWS/adminpasswd' "$ROOT/tests/e2e/Dockerfile.custom-ols"; then
        fail "release packaging is missing build-time WebAdmin password removal/checks"
    else
        echo "OK: release packaging removes build-time WebAdmin password"
    fi
fi

if ! grep -q 'HttpContext tmpCtx;' "$REWRITE_PATCH"; then
    fail "rewrite patch no longer creates the temporary HttpContext required by parseRules"
else
    echo "OK: rewrite patch keeps parseRules context"
fi

if ! grep -q 'getStatusCode()' "$REWRITE_PATCH"; then
    fail "rewrite patch no longer exposes RewriteEngine::getStatusCode()"
else
    echo "OK: rewrite patch keeps redirect status propagation"
fi

if ! grep -q 'text_len > 1024 \* 1024' "$REWRITE_PATCH"; then
    fail "rewrite patch is missing the 1MB parse input limit"
else
    echo "OK: rewrite patch keeps parse input limit"
fi

if awk '
    /if \(!rules_text \|\| text_len <= 0\)/ {
        getline next_line
        if (next_line ~ /if \(text_len > 1024 \* 1024\)/)
            bad = 1
    }
    END { exit bad ? 0 : 1 }
' "$REWRITE_PATCH"; then
    fail "rewrite patch has the known broken guard: null/empty check falls through to size check"
else
    echo "OK: rewrite patch guard is braced/structured"
fi

if [ -n "${OLS_SOURCE_DIR:-}" ]; then
    if [ ! -d "$OLS_SOURCE_DIR" ]; then
        fail "OLS_SOURCE_DIR does not exist: $OLS_SOURCE_DIR"
    else
        tmpdir="$(mktemp -d)"
        trap 'rm -rf "$tmpdir"' EXIT
        cp -a "$OLS_SOURCE_DIR/." "$tmpdir/src/"
        for patch_file in "$ROOT"/patches/000{1,2,3,4}-*.patch; do
            echo "Dry-run apply: $(basename "$patch_file")"
            (cd "$tmpdir/src" && patch -p1 --dry-run < "$patch_file" >/dev/null)
            (cd "$tmpdir/src" && patch -p1 < "$patch_file" >/dev/null)
        done
        echo "OK: patches dry-run apply to OLS_SOURCE_DIR"

        APPLIED="$tmpdir/src"
        if ! grep -q 'set_php_config_value' "$APPLIED/include/ls.h" ||
            ! grep -q 'set_php_config_flag' "$APPLIED/include/ls.h" ||
            ! grep -q 'litehttpd_lsiapi_extensions_v1' "$APPLIED/src/lsiapi/lsiapilib.cpp" ||
            ! grep -q 'pCfg->buildLsapiEnv();' "$APPLIED/src/lsiapi/lsiapilib.cpp"; then
            fail "patch 0001 semantic check failed: PHPConfig LSIAPI path is incomplete"
        else
            echo "OK: patch 0001 exposes PHPConfig LSIAPI, ABI marker, and rebuilds LSAPI env"
        fi

        if ! grep -q 'parse_rewrite_rules' "$APPLIED/include/ls.h" ||
            ! grep -q 'exec_rewrite_rules' "$APPLIED/include/ls.h" ||
            ! grep -q 'getStatusCode()' "$APPLIED/src/http/rewriteengine.h" ||
            ! grep -q 'pReq->postRewriteProcess' "$APPLIED/src/lsiapi/lsiapilib.cpp" ||
            ! grep -q 'REWRITE_REDIR' "$APPLIED/src/lsiapi/lsiapilib.cpp"; then
            fail "patch 0002 semantic check failed: Rewrite LSIAPI path is incomplete"
        else
            echo "OK: patch 0002 exposes rewrite parse/exec/free and redirect status flow"
        fi

        if ! grep -q 'apacheconf_parser.c' "$APPLIED/src/main/CMakeLists.txt" ||
            ! grep -q 'ols_config_writer.c' "$APPLIED/src/main/CMakeLists.txt" ||
            ! grep -q 'readApacheConf: parsing' "$APPLIED/src/main/plainconf.cpp" ||
            ! grep -q 'ap_parse_config' "$APPLIED/src/main/plainconf.cpp" ||
            ! grep -q 'ols_write_config' "$APPLIED/src/main/plainconf.cpp" ||
            ! grep -q 'loadConfFile(outputDir)' "$APPLIED/src/main/plainconf.cpp"; then
            fail "patch 0003 semantic check failed: readApacheConf integration is incomplete"
        else
            echo "OK: patch 0003 wires readApacheConf parser/writer into OLS startup config"
        fi

        if ! grep -q 'indexes_disabled' "$APPLIED/src/http/httpreq.cpp" ||
            ! grep -q 'Options -Indexes' "$APPLIED/src/http/httpreq.cpp" ||
            ! grep -q 'm_pContext->isAutoIndexOff()' "$APPLIED/src/http/httpreq.cpp" ||
            ! grep -q 'return SC_403;' "$APPLIED/src/http/httpreq.cpp"; then
            fail "patch 0004 semantic check failed: autoindex 403 path is incomplete"
        elif ! awk '
            /if \(!m_pContext->isAppContext\(\)\)/ {
                pending = 1
                next
            }
            pending && /^[[:space:]]*\{/ {
                braced = 1
                pending = 0
                next
            }
            pending && /^[[:space:]]*$/ {
                next
            }
            pending {
                pending = 0
            }
            END { exit braced ? 0 : 1 }
        ' "$APPLIED/src/http/httpreq.cpp"; then
            fail "patch 0004 leaves if (!isAppContext()) unbraced around autoindex handling"
        else
            echo "OK: patch 0004 keeps autoindex 403 handling explicit and braced"
        fi
    fi
fi

echo "========================================"
exit "$FAIL"
