#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COMPOSE_FILE="${SCRIPT_DIR}/docker-compose.full.yml"

# shellcheck source=tests/e2e/apps-matrix/lib/matrix_common.sh
source "${SCRIPT_DIR}/lib/matrix_common.sh"
# shellcheck source=tests/e2e/apps-matrix/config/budgets.env
source "${SCRIPT_DIR}/config/budgets.env"

TIER="${MATRIX_TIER:-nightly}"
OUT_DIR="${MATRIX_OUT_DIR:-${SCRIPT_DIR}/out/full-matrix}"
FIXTURE_ROOT="${MATRIX_FIXTURE_ROOT:-${OUT_DIR}/fixtures/docroots}"
KEEP_STACK="${MATRIX_KEEP_STACK:-0}"
CONTINUE_ON_FAILURE="${MATRIX_CONTINUE_ON_FAILURE:-0}"
PHASE="${MATRIX_FULL_PHASE:-all}"
DB_DUMP_DIR="${MATRIX_DB_DUMP_DIR:-${OUT_DIR}/db-dumps}"
COMPOSE_BUILD="${MATRIX_COMPOSE_BUILD:-1}"
WORDPRESS_SCENARIOS="wordpress-core,wordpress-redirection,wordpress-w3-total-cache,wordpress-litespeed-cache,wordpress-wordfence,wordpress-ewww,wordpress-wp-optimize"
CMS_SCENARIOS="drupal,nextcloud,joomla,mediawiki"
FRAMEWORK_SCENARIOS="laravel"
HEAVY_SCENARIOS="wordpress-core,wordpress-redirection,wordpress-w3-total-cache,wordpress-litespeed-cache,wordpress-wordfence,wordpress-ewww,wordpress-wp-optimize,laravel,drupal,nextcloud,joomla,mediawiki"

export MATRIX_OUT_DIR="${OUT_DIR}"
export MATRIX_FIXTURE_ROOT="${FIXTURE_ROOT}"
export MATRIX_FIXTURE_MODE="${MATRIX_FIXTURE_MODE:-real}"
export MATRIX_ENGINES="${MATRIX_ENGINES:-apache,ols_module,ols_native}"
export MATRIX_REAL_DB_RUNTIME_HOST="${MATRIX_REAL_DB_RUNTIME_HOST:-db}"
export MATRIX_REAL_DB_RUNTIME_PORT="${MATRIX_REAL_DB_RUNTIME_PORT:-3306}"

export MATRIX_APACHE_URL="${MATRIX_APACHE_URL:-http://localhost:${MATRIX_APACHE_PORT:-18080}}"
export MATRIX_OLS_MODULE_URL="${MATRIX_OLS_MODULE_URL:-http://localhost:${MATRIX_OLS_MODULE_PORT:-38080}}"
export MATRIX_OLS_NATIVE_URL="${MATRIX_OLS_NATIVE_URL:-http://localhost:${MATRIX_OLS_NATIVE_PORT:-28080}}"
export MATRIX_READY_PATH="${MATRIX_READY_PATH:-/_probe/server.php}"
export MATRIX_HTTP_HOST="${MATRIX_HTTP_HOST:-localhost}"
export MATRIX_APP_ROUTE_PROBE="${MATRIX_APP_ROUTE_PROBE:-1}"

# shellcheck disable=SC2317  # Called by the EXIT trap.
cleanup() {
    if [[ "${KEEP_STACK}" != "1" ]]; then
        compose_down
    fi
}
trap cleanup EXIT

compose_down() {
    docker compose -f "${COMPOSE_FILE}" down -v >/dev/null 2>&1 || true
}

scenario_list() {
    local raw="${MATRIX_SCENARIOS:-${MATRIX_SCENARIO:-wordpress-core}}"
    case "${raw}" in
        all)
            bash "${SCRIPT_DIR}/run_matrix.sh" --list
            return
            ;;
        heavy) raw="${HEAVY_SCENARIOS}" ;;
        wordpress) raw="${WORDPRESS_SCENARIOS}" ;;
        cms) raw="${CMS_SCENARIOS}" ;;
        framework) raw="${FRAMEWORK_SCENARIOS}" ;;
    esac
    printf '%s\n' "${raw}" | tr ', ' '\n' | sed '/^$/d'
}

safe_project_suffix() {
    local raw="$1"
    printf '%s\n' "${raw}" | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]/-/g'
}

engine_enabled() {
    local engine="$1"
    local raw=",${MATRIX_ENGINES},"
    raw="${raw//[[:space:]]/}"
    [[ "${raw}" == *",${engine},"* ]]
}

compose_services() {
    printf '%s\n' apache ols_module
    if engine_enabled ols_native; then
        printf '%s\n' ols_native
    fi
}

pull_compose_images() {
    local -a services=("$@")
    docker compose -f "${COMPOSE_FILE}" pull "${services[@]}"
}

wait_for_url() {
    local label="$1"
    local url="$2"
    local i
    for i in $(seq 1 90); do
        if curl -fsS "${url}" >/dev/null 2>&1; then
            matrix_ok "${label} ready after ${i}s"
            return 0
        fi
        sleep 1
    done
    matrix_fail "${label} did not become ready at ${url}"
    return 1
}

ready_url() {
    local base="$1"
    local scenario="${2:-}"
    local path="${MATRIX_READY_PATH:-/}"
    case "${scenario}" in
        drupal)
            path="${MATRIX_READY_PATH_DRUPAL:-/core/misc/drupal.js}"
            ;;
    esac
    if [[ "${path}" != /* ]]; then
        path="/${path}"
    fi
    printf '%s%s\n' "${base%/}" "${path}"
}

wait_for_db() {
    local i
    for i in $(seq 1 90); do
        if docker compose -f "${COMPOSE_FILE}" exec -T db \
            mariadb -uroot -p"${MATRIX_REAL_DB_ROOT_PASSWORD:-rootpass}" -e "SELECT 1" >/dev/null 2>&1; then
            matrix_ok "db ready after ${i}s"
            return 0
        fi
        sleep 1
    done
    matrix_fail "db did not become ready"
    return 1
}

collect_logs() {
    local scenario="$1"
    local log_dir="${OUT_DIR}/logs/${scenario}"
    mkdir -p "${log_dir}"
    docker compose -f "${COMPOSE_FILE}" ps >"${log_dir}/compose-ps.txt" 2>&1 || true
    docker compose -f "${COMPOSE_FILE}" logs --no-color >"${log_dir}/compose.log" 2>&1 || true
    collect_app_logs "${scenario}" "${log_dir}"
}

copy_redacted_log() {
    local source="$1"
    local target="$2"
    [[ -f "${source}" ]] || return 0
    mkdir -p "$(dirname "${target}")"
    SOURCE="${source}" \
    TARGET="${target}" \
    MATRIX_REAL_DB_ROOT_PASSWORD="${MATRIX_REAL_DB_ROOT_PASSWORD:-rootpass}" \
    MATRIX_REAL_DB_PASSWORD="${MATRIX_REAL_DB_PASSWORD:-matrixpass}" \
    MATRIX_WORDPRESS_ADMIN_PASSWORD="${MATRIX_WORDPRESS_ADMIN_PASSWORD:-AppsMatrixAdmin123!}" \
    python3 - <<'PY'
import os
from pathlib import Path

source = Path(os.environ["SOURCE"])
target = Path(os.environ["TARGET"])
text = source.read_text(encoding="utf-8", errors="replace")
for name in (
    "MATRIX_REAL_DB_ROOT_PASSWORD",
    "MATRIX_REAL_DB_PASSWORD",
    "MATRIX_WORDPRESS_ADMIN_PASSWORD",
):
    secret = os.environ.get(name, "")
    if secret:
        text = text.replace(secret, "<redacted>")
target.write_text(text, encoding="utf-8")
PY
}

collect_app_logs() {
    local scenario="$1"
    local log_dir="$2"
    local engine root source
    for engine in apache ols_module ols_native; do
        engine_enabled "${engine}" || continue
        root="${FIXTURE_ROOT}/${engine}/${scenario}"
        case "${scenario}" in
            laravel)
                for source in "${root}"/storage/logs/*.log; do
                    [[ -f "${source}" ]] || continue
                    copy_redacted_log "${source}" "${log_dir}/${engine}-$(basename "${source}")"
                done
                ;;
            nextcloud)
                copy_redacted_log "${root}/data/nextcloud.log" "${log_dir}/${engine}-nextcloud.log"
                ;;
            joomla)
                for source in "${root}"/administrator/logs/*.php "${root}"/logs/*.php; do
                    [[ -f "${source}" ]] || continue
                    copy_redacted_log "${source}" "${log_dir}/${engine}-$(basename "${source}")"
                done
                ;;
        esac
    done
}

csv_append() {
    local file="$1"
    shift
    mkdir -p "$(dirname "${file}")"
    python3 - "$file" "$@" <<'PY'
import csv
import sys

path = sys.argv[1]
row = sys.argv[2:]
with open(path, "a", encoding="utf-8", newline="") as fh:
    csv.writer(fh, lineterminator="\n").writerow(row)
PY
}

init_full_timing() {
    local file="${OUT_DIR}/full-matrix-timing.csv"
    if [[ ! -s "${file}" ]]; then
        csv_append "${file}" scenario phase duration_ms result budget_ms budget_status
    fi
}

scenario_budget_var() {
    local scenario="$1"
    local phase="$2"
    local normalized
    normalized="$(printf '%s' "${scenario}" | tr '[:lower:]-' '[:upper:]_')"
    printf 'MATRIX_BUDGET_%s_%s_MS\n' "${normalized}" "$(printf '%s' "${phase}" | tr '[:lower:]' '[:upper:]')"
}

scenario_budget_ms() {
    local scenario="$1"
    local phase="$2"
    local var default_var
    var="$(scenario_budget_var "${scenario}" "${phase}")"
    if [[ -n "${!var:-}" ]]; then
        printf '%s\n' "${!var}"
        return
    fi
    default_var="MATRIX_BUDGET_DEFAULT_$(printf '%s' "${phase}" | tr '[:lower:]' '[:upper:]')_MS"
    printf '%s\n' "${!default_var:-}"
}

validate_full_config() {
    local budget_ms="${MATRIX_SCENARIO_BUDGET_MS:-}"
    local required_engine
    case "${PHASE}" in
        install|verify|all) ;;
        *)
            matrix_fail "unknown MATRIX_FULL_PHASE=${PHASE}; expected install, verify, or all"
            return 1
            ;;
    esac

    if [[ -n "${budget_ms}" && ! "${budget_ms}" =~ ^[0-9]+$ ]]; then
        matrix_fail "MATRIX_SCENARIO_BUDGET_MS must be a non-negative integer"
        return 1
    fi
    case "${COMPOSE_BUILD}" in
        0|1) ;;
        *)
            matrix_fail "MATRIX_COMPOSE_BUILD must be 0 or 1"
            return 1
            ;;
    esac
    if [[ "${PHASE}" != "install" ]]; then
        for required_engine in apache ols_module; do
            if ! engine_enabled "${required_engine}"; then
                matrix_fail "MATRIX_ENGINES must include apache and ols_module for ${PHASE} phase"
                return 1
            fi
        done
        if [[ "${COMPOSE_BUILD}" == "0" ]]; then
            if [[ -z "${MATRIX_APACHE_IMAGE:-}" ]]; then
                matrix_fail "MATRIX_APACHE_IMAGE is required when MATRIX_COMPOSE_BUILD=0"
                return 1
            fi
            if [[ -z "${MATRIX_OLS_MODULE_IMAGE:-}" ]]; then
                matrix_fail "MATRIX_OLS_MODULE_IMAGE is required when MATRIX_COMPOSE_BUILD=0"
                return 1
            fi
        fi
    fi
}

record_full_timing() {
    local scenario="$1"
    local phase="$2"
    local duration_ms="$3"
    local result="$4"
    local budget_ms="${MATRIX_SCENARIO_BUDGET_MS:-}"
    local status=""
    if [[ -z "${budget_ms}" ]]; then
        budget_ms="$(scenario_budget_ms "${scenario}" "${phase}")"
    fi
    if [[ -n "${budget_ms}" && ! "${budget_ms}" =~ ^[0-9]+$ ]]; then
        matrix_fail "${scenario} ${phase} budget must be a non-negative integer: ${budget_ms}"
        return 1
    fi
    if [[ -n "${budget_ms}" ]]; then
        if (( duration_ms > budget_ms )); then
            status="over_budget"
        else
            status="ok"
        fi
    fi
    csv_append "${OUT_DIR}/full-matrix-timing.csv" "${scenario}" "${phase}" "${duration_ms}" "${result}" "${budget_ms}" "${status}"
    if [[ "${status}" == "over_budget" && "${MATRIX_ENFORCE_SCENARIO_BUDGET:-0}" == "1" ]]; then
        matrix_fail "${scenario} ${phase} exceeded budget: ${duration_ms}ms > ${budget_ms}ms"
        return 1
    fi
}

db_dump_file() {
    local scenario="$1"
    printf '%s/%s.sql.gz\n' "${DB_DUMP_DIR}" "${scenario}"
}

dump_scenario_db() {
    local scenario="$1"
    local db_name="${scenario//-/_}"
    db_name="matrix_${db_name}"
    local dump_file
    dump_file="$(db_dump_file "${scenario}")"
    mkdir -p "$(dirname "${dump_file}")"
    docker compose -f "${COMPOSE_FILE}" exec -T db \
        mariadb-dump -uroot -p"${MATRIX_REAL_DB_ROOT_PASSWORD:-rootpass}" \
        --single-transaction --skip-lock-tables "${db_name}" \
        | gzip -c >"${dump_file}"
}

restore_scenario_db() {
    local scenario="$1"
    local db_name="${scenario//-/_}"
    db_name="matrix_${db_name}"
    local dump_file
    dump_file="$(db_dump_file "${scenario}")"
    if [[ ! -s "${dump_file}" ]]; then
        matrix_fail "missing DB dump for ${scenario}: ${dump_file}"
        return 1
    fi
    docker compose -f "${COMPOSE_FILE}" exec -T db \
        mariadb -uroot -p"${MATRIX_REAL_DB_ROOT_PASSWORD:-rootpass}" \
        -e "CREATE DATABASE IF NOT EXISTS \`${db_name}\` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
    gzip -dc "${dump_file}" | docker compose -f "${COMPOSE_FILE}" exec -T db \
        mariadb -uroot -p"${MATRIX_REAL_DB_ROOT_PASSWORD:-rootpass}" "${db_name}"
}

finish_scenario_failure() {
    local scenario="$1"
    local phase="$2"
    local started="$3"
    local ended duration_ms
    ended="$(date +%s%3N)"
    duration_ms=$((ended - started))
    record_full_timing "${scenario}" "${phase}" "${duration_ms}" failed || true
    return 1
}

run_scenario() {
    local scenario="$1"
    local suffix
    local started ended duration_ms result
    local -a services
    suffix="$(safe_project_suffix "${scenario}")"

    export COMPOSE_PROJECT_NAME="${MATRIX_COMPOSE_PROJECT_PREFIX:-apps-matrix}-${suffix}"
    export MATRIX_APACHE_VOLUME_ROOT="${FIXTURE_ROOT}/apache/${scenario}"
    export MATRIX_OLS_MODULE_VOLUME_ROOT="${FIXTURE_ROOT}/ols_module/${scenario}"
    export MATRIX_OLS_NATIVE_VOLUME_ROOT="${FIXTURE_ROOT}/ols_native/${scenario}"
    export MATRIX_APACHE_DOCROOT="${MATRIX_APACHE_VOLUME_ROOT}"
    export MATRIX_OLS_MODULE_DOCROOT="${MATRIX_OLS_MODULE_VOLUME_ROOT}"
    export MATRIX_OLS_NATIVE_DOCROOT="${MATRIX_OLS_NATIVE_VOLUME_ROOT}"
    export MATRIX_APACHE_CONTAINER_DOCROOT="/var/www/html"
    export MATRIX_OLS_MODULE_CONTAINER_DOCROOT="/var/www/vhosts/localhost/html"
    export MATRIX_OLS_NATIVE_CONTAINER_DOCROOT="/var/www/vhosts/localhost/html"
    if [[ "${scenario}" == "laravel" ]]; then
        export MATRIX_APACHE_DOCROOT="${MATRIX_APACHE_VOLUME_ROOT}/public"
        export MATRIX_OLS_MODULE_DOCROOT="${MATRIX_OLS_MODULE_VOLUME_ROOT}/public"
        export MATRIX_OLS_NATIVE_DOCROOT="${MATRIX_OLS_NATIVE_VOLUME_ROOT}/public"
        export MATRIX_APACHE_CONTAINER_DOCROOT="/var/www/html/public"
        export MATRIX_OLS_MODULE_CONTAINER_DOCROOT="/var/www/vhosts/localhost/html/public"
        export MATRIX_OLS_NATIVE_CONTAINER_DOCROOT="/var/www/vhosts/localhost/html/public"
    elif [[ "${scenario}" == "drupal" ]]; then
        export MATRIX_APACHE_DOCROOT="${MATRIX_APACHE_VOLUME_ROOT}/web"
        export MATRIX_OLS_MODULE_DOCROOT="${MATRIX_OLS_MODULE_VOLUME_ROOT}/web"
        export MATRIX_OLS_NATIVE_DOCROOT="${MATRIX_OLS_NATIVE_VOLUME_ROOT}/web"
        export MATRIX_APACHE_CONTAINER_DOCROOT="/var/www/html/web"
        export MATRIX_OLS_MODULE_CONTAINER_DOCROOT="/var/www/vhosts/localhost/html/web"
        export MATRIX_OLS_NATIVE_CONTAINER_DOCROOT="/var/www/vhosts/localhost/html/web"
    fi

    matrix_banner "Apps Matrix Full Matrix: ${scenario}"
    started="$(date +%s%3N)"

    if [[ "${PHASE}" == "install" || "${PHASE}" == "all" ]]; then
        if [[ "${MATRIX_REAL_RUN_APP_INSTALL:-0}" == "1" ]]; then
            docker compose -f "${COMPOSE_FILE}" up -d db || {
                finish_scenario_failure "${scenario}" "${PHASE}" "${started}"
                return 1
            }
            wait_for_db || {
                finish_scenario_failure "${scenario}" "${PHASE}" "${started}"
                return 1
            }
        fi
        if [[ "${MATRIX_SKIP_INSTALL:-0}" != "1" ]]; then
            bash "${SCRIPT_DIR}/run_matrix.sh" --install-only --scenario "${scenario}" || {
                finish_scenario_failure "${scenario}" "${PHASE}" "${started}"
                return 1
            }
        fi
        if [[ "${MATRIX_REAL_RUN_APP_INSTALL:-0}" == "1" ]]; then
            dump_scenario_db "${scenario}" || {
                finish_scenario_failure "${scenario}" "${PHASE}" "${started}"
                return 1
            }
        fi
    fi

    if [[ "${PHASE}" == "install" ]]; then
        if [[ "${KEEP_STACK}" != "1" ]]; then
            compose_down
        fi
        ended="$(date +%s%3N)"
        duration_ms=$((ended - started))
        record_full_timing "${scenario}" "${PHASE}" "${duration_ms}" ok || return 1
        return 0
    fi

    if [[ "${PHASE}" == "verify" && "${MATRIX_REAL_RUN_APP_INSTALL:-0}" == "1" ]]; then
        docker compose -f "${COMPOSE_FILE}" up -d db || {
            finish_scenario_failure "${scenario}" "${PHASE}" "${started}"
            return 1
        }
        wait_for_db || {
            finish_scenario_failure "${scenario}" "${PHASE}" "${started}"
            return 1
        }
        restore_scenario_db "${scenario}" || {
            finish_scenario_failure "${scenario}" "${PHASE}" "${started}"
            return 1
        }
    fi

    mapfile -t services < <(compose_services)
    compose_up_args=(-d)
    if [[ "${COMPOSE_BUILD}" == "1" ]]; then
        compose_up_args+=(--build)
    else
        compose_up_args+=(--no-build)
        pull_compose_images "${services[@]}" || {
            finish_scenario_failure "${scenario}" "${PHASE}" "${started}"
            return 1
        }
    fi
    docker compose -f "${COMPOSE_FILE}" up "${compose_up_args[@]}" "${services[@]}" || {
        finish_scenario_failure "${scenario}" "${PHASE}" "${started}"
        return 1
    }

    wait_for_url apache "$(ready_url "${MATRIX_APACHE_URL}" "${scenario}")" || {
        finish_scenario_failure "${scenario}" "${PHASE}" "${started}"
        return 1
    }
    wait_for_url ols_module "$(ready_url "${MATRIX_OLS_MODULE_URL}" "${scenario}")" || {
        finish_scenario_failure "${scenario}" "${PHASE}" "${started}"
        return 1
    }
    if engine_enabled ols_native; then
        wait_for_url ols_native "$(ready_url "${MATRIX_OLS_NATIVE_URL}" "${scenario}")" || {
            finish_scenario_failure "${scenario}" "${PHASE}" "${started}"
            return 1
        }
    fi

    result=ok
    bash "${SCRIPT_DIR}/run_matrix.sh" --scenario "${scenario}" --tier "${TIER}" || result=failed

    collect_logs "${scenario}"
    if [[ "${KEEP_STACK}" != "1" ]]; then
        compose_down
    fi
    ended="$(date +%s%3N)"
    duration_ms=$((ended - started))
    record_full_timing "${scenario}" "${PHASE}" "${duration_ms}" "${result}" || return 1
    [[ "${result}" == "ok" ]]
}

matrix_banner "Apps Matrix Full Matrix"
mkdir -p "${OUT_DIR}"
validate_full_config
init_full_timing
matrix_summary_init "${OUT_DIR}/summary.csv"
export MATRIX_SUMMARY_APPEND=1

failed=0
mapfile -t selected_scenarios < <(scenario_list)
for scenario in "${selected_scenarios[@]}"; do
    [[ -n "${scenario}" ]] || continue
    if ! run_scenario "${scenario}"; then
        failed=1
        collect_logs "${scenario}"
        if [[ "${KEEP_STACK}" != "1" ]]; then
            compose_down
        fi
        if [[ "${CONTINUE_ON_FAILURE}" != "1" ]]; then
            break
        fi
    fi
done

if [[ "${PHASE}" != "install" && "${failed}" == "0" ]]; then
    python3 "${SCRIPT_DIR}/lib/validate_results.py" \
        --results-dir "${OUT_DIR}/results" \
        --schema "${SCRIPT_DIR}/lib/result_schema.json"
fi

python3 "${SCRIPT_DIR}/lib/tune_budgets.py" \
    --out-dir "${OUT_DIR}" \
    --include-per-engine \
    >"${OUT_DIR}/budget-suggestions.env" || true

python3 "${SCRIPT_DIR}/lib/collect_artifact_manifest.py" \
    --out-dir "${OUT_DIR}" \
    --mode "${MATRIX_ARTIFACT_MODE:-summary}" \
    >"${OUT_DIR}/artifact-manifest.txt" || true

if [[ "${failed}" == "0" ]]; then
    matrix_ok "full matrix ${PHASE} artifacts written to ${OUT_DIR}"
else
    matrix_fail "full matrix ${PHASE} failed; artifacts written to ${OUT_DIR}"
fi

exit "${failed}"
