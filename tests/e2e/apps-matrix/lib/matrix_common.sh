#!/usr/bin/env bash
set -euo pipefail

MATRIX_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MATRIX_OUT_DIR="${MATRIX_OUT_DIR:-${MATRIX_DIR}/out}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

mkdir -p "${MATRIX_OUT_DIR}"

matrix_banner() {
    echo -e "${CYAN}== $* ==${NC}"
}

matrix_warn() {
    echo -e "${YELLOW}WARN${NC} $*"
}

matrix_fail() {
    echo -e "${RED}FAIL${NC} $*"
}

matrix_ok() {
    echo -e "${GREEN}OK${NC} $*"
}

matrix_summary_init() {
    local file="$1"
    cat >"${file}" <<'EOF'
scenario,case_id,category,result,known_diff_reason
EOF
}

matrix_summary_append() {
    local file="$1"
    local scenario="$2"
    local case_id="$3"
    local category="$4"
    local result="$5"
    local reason="${6:-}"
    printf '%s,%s,%s,%s,%s\n' \
        "${scenario}" "${case_id}" "${category}" "${result}" "${reason}" >>"${file}"
}

matrix_print_case_plan() {
    local yaml="$1"
    awk '
        $1 == "-"{ in_case=1; id=""; desc=""; category="" }
        /id:/      { sub(/.*id:[[:space:]]*/, ""); id=$0 }
        /desc:/    { sub(/.*desc:[[:space:]]*/, ""); desc=$0 }
        /category:/{ sub(/.*category:[[:space:]]*/, ""); category=$0 }
        in_case && id != "" && desc != "" && category != "" {
            printf "  - [%s] %s (%s)\n", id, desc, category
            in_case=0
        }
    ' "${yaml}"
}
