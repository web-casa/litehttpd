#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

WORDPRESS_SCENARIOS="wordpress-core,wordpress-redirection,wordpress-w3-total-cache,wordpress-litespeed-cache,wordpress-wordfence,wordpress-ewww,wordpress-wp-optimize"
CMS_SCENARIOS="drupal,nextcloud,joomla,mediawiki"
FRAMEWORK_SCENARIOS="laravel"
HEAVY_SCENARIOS="${WORDPRESS_SCENARIOS},${FRAMEWORK_SCENARIOS},${CMS_SCENARIOS}"

scenario_list() {
    local raw="${MATRIX_SCENARIOS:-heavy}"
    case "${raw}" in
        heavy|all) raw="${HEAVY_SCENARIOS}" ;;
        wordpress) raw="${WORDPRESS_SCENARIOS}" ;;
        cms) raw="${CMS_SCENARIOS}" ;;
        framework) raw="${FRAMEWORK_SCENARIOS}" ;;
    esac
    printf '%s\n' "${raw}" | tr ', ' '\n' | sed '/^$/d'
}

export MATRIX_FIXTURE_MODE="${MATRIX_FIXTURE_MODE:-real}"
export MATRIX_REAL_ALLOW_NETWORK="${MATRIX_REAL_ALLOW_NETWORK:-1}"
export MATRIX_REAL_RUN_APP_INSTALL=0
export MATRIX_REAL_VALIDATE_CONFIG="${MATRIX_REAL_VALIDATE_CONFIG:-1}"
export MATRIX_ENGINES="${MATRIX_ENGINES:-apache}"

mapfile -t selected_scenarios < <(scenario_list)
for scenario in "${selected_scenarios[@]}"; do
    [[ -n "${scenario}" ]] || continue
    bash "${SCRIPT_DIR}/run_matrix.sh" --install-only --scenario "${scenario}"
done
