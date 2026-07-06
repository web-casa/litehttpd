#!/usr/bin/env bash
set -euo pipefail

MATRIX_FIXTURE_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MATRIX_DIR="${MATRIX_DIR:-$(cd "${MATRIX_FIXTURE_LIB_DIR}/.." && pwd)}"
MATRIX_OUT_DIR="${MATRIX_OUT_DIR:-${MATRIX_DIR}/out}"

matrix_install_fixture() {
    local scenario="$1"
    local cases_yaml="$2"

    if ! command -v python3 >/dev/null 2>&1; then
        matrix_fail "python3 is required for apps-matrix fixture provisioning"
        return 1
    fi

    if [[ "${MATRIX_FIXTURE_MODE:-stub}" == "real" ]]; then
        # shellcheck source=tests/e2e/apps-matrix/lib/real_install_common.sh
        source "${MATRIX_DIR}/lib/real_install_common.sh"
        matrix_real_install_fixture "${scenario}" "${cases_yaml}"
        return $?
    fi

    python3 "${MATRIX_DIR}/lib/provision_fixture.py" \
        --scenario "${scenario}" \
        --cases "${cases_yaml}" \
        --probes-dir "${MATRIX_DIR}/probes" \
        --out-dir "${MATRIX_OUT_DIR}"
}
