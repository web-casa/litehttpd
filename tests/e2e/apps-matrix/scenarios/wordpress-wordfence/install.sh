#!/usr/bin/env bash
set -euo pipefail

SCENARIO="${1:-wordpress-wordfence}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# shellcheck source=tests/e2e/apps-matrix/lib/matrix_common.sh
source "${ROOT_DIR}/lib/matrix_common.sh"
# shellcheck source=tests/e2e/apps-matrix/lib/fixture_common.sh
source "${ROOT_DIR}/lib/fixture_common.sh"

matrix_install_fixture "${SCENARIO}" "${SCRIPT_DIR}/cases.yaml"
