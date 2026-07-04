#!/usr/bin/env bash
set -euo pipefail

MATRIX_REAL_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MATRIX_DIR="${MATRIX_DIR:-$(cd "${MATRIX_REAL_LIB_DIR}/.." && pwd)}"
MATRIX_OUT_DIR="${MATRIX_OUT_DIR:-${MATRIX_DIR}/out}"

# shellcheck disable=SC1091
source "${MATRIX_DIR}/config/versions.env"

MATRIX_REAL_ALLOW_NETWORK="${MATRIX_REAL_ALLOW_NETWORK:-0}"
MATRIX_REAL_RUN_APP_INSTALL="${MATRIX_REAL_RUN_APP_INSTALL:-0}"
MATRIX_REAL_DOWNLOAD_DIR="${MATRIX_REAL_DOWNLOAD_DIR:-${MATRIX_OUT_DIR}/downloads}"
MATRIX_REAL_SOURCE_ROOT="${MATRIX_REAL_SOURCE_ROOT:-${MATRIX_OUT_DIR}/real-sources}"
MATRIX_REAL_LOG_DIR="${MATRIX_REAL_LOG_DIR:-${MATRIX_OUT_DIR}/real-install-logs}"
MATRIX_REAL_DEEP_CONFIG="${MATRIX_REAL_DEEP_CONFIG:-1}"
MATRIX_REAL_VALIDATE_CONFIG="${MATRIX_REAL_VALIDATE_CONFIG:-1}"
MATRIX_REAL_STRICT_PLUGIN_INSTALL="${MATRIX_REAL_STRICT_PLUGIN_INSTALL:-auto}"
MATRIX_REAL_HEALTH_PROBES="${MATRIX_REAL_HEALTH_PROBES:-1}"
MATRIX_REAL_ENFORCE_HEALTH_PROBES="${MATRIX_REAL_ENFORCE_HEALTH_PROBES:-1}"

matrix_real_selected_engines() {
    local raw="${MATRIX_ENGINES:-apache,ols_module,ols_native}"
    local old_ifs="${IFS}"
    local engine
    IFS=,
    for engine in ${raw}; do
        engine="${engine//[[:space:]]/}"
        [[ -n "${engine}" ]] && printf '%s\n' "${engine}"
    done
    IFS="${old_ifs}"
}

matrix_real_docroot() {
    local engine="$1"
    local scenario="$2"
    local env_name="MATRIX_${engine^^}_DOCROOT"
    local fixture_root="${MATRIX_FIXTURE_ROOT:-${MATRIX_OUT_DIR}/fixtures/docroots}"
    if [[ -n "${!env_name:-}" ]]; then
        printf '%s\n' "${!env_name}"
        return
    fi
    case "${scenario}" in
        laravel) printf '%s/%s/%s/public\n' "${fixture_root}" "${engine}" "${scenario}" ;;
        drupal) printf '%s/%s/%s/web\n' "${fixture_root}" "${engine}" "${scenario}" ;;
        *) printf '%s/%s/%s\n' "${fixture_root}" "${engine}" "${scenario}" ;;
    esac
}

matrix_real_volume_root() {
    local engine="$1"
    local scenario="$2"
    local env_name="MATRIX_${engine^^}_VOLUME_ROOT"
    local docroot
    if [[ -n "${!env_name:-}" ]]; then
        printf '%s\n' "${!env_name}"
        return
    fi
    docroot="$(matrix_real_docroot "${engine}" "${scenario}")"
    case "${scenario}" in
        laravel|drupal)
            case "${docroot}" in
                */public|*/web) dirname "${docroot}" ;;
                *) printf '%s\n' "${docroot}" ;;
            esac
            ;;
        *) printf '%s\n' "${docroot}" ;;
    esac
}

matrix_real_require_tool() {
    local tool="$1"
    command -v "${tool}" >/dev/null 2>&1 || {
        matrix_fail "real fixture install requires '${tool}'"
        return 1
    }
}

matrix_real_safe_identifier() {
    local raw="$1"
    local identifier="${raw//-/_}"
    if [[ ! "${identifier}" =~ ^[A-Za-z0-9_]+$ ]]; then
        matrix_fail "unsafe database identifier: ${raw}"
        return 1
    fi
    printf '%s\n' "${identifier}"
}

matrix_real_db_name() {
    local scenario="$1"
    local suffix
    suffix="$(matrix_real_safe_identifier "${scenario}")"
    printf 'matrix_%s\n' "${suffix}"
}

matrix_real_mysql_root() {
    matrix_real_require_tool mysql
    mysql -h"${MATRIX_REAL_DB_INSTALL_HOST}" -P"${MATRIX_REAL_DB_INSTALL_PORT}" \
        -u"${MATRIX_REAL_DB_ROOT_USER}" -p"${MATRIX_REAL_DB_ROOT_PASSWORD}" "$@"
}

matrix_real_create_database() {
    local db_name="$1"
    db_name="$(matrix_real_safe_identifier "${db_name}")"
    matrix_real_mysql_root \
        -e "CREATE DATABASE IF NOT EXISTS \`${db_name}\` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
}

matrix_real_db_install_endpoint() {
    printf '%s:%s\n' "${MATRIX_REAL_DB_INSTALL_HOST}" "${MATRIX_REAL_DB_INSTALL_PORT}"
}

matrix_real_db_runtime_endpoint() {
    printf '%s:%s\n' "${MATRIX_REAL_DB_RUNTIME_HOST}" "${MATRIX_REAL_DB_RUNTIME_PORT}"
}

matrix_real_rewrite_runtime_db_endpoint() {
    local file="$1"
    [[ -f "${file}" ]] || return 0
    INSTALL_HOST="${MATRIX_REAL_DB_INSTALL_HOST}" \
    INSTALL_PORT="${MATRIX_REAL_DB_INSTALL_PORT}" \
    INSTALL_ENDPOINT="$(matrix_real_db_install_endpoint)" \
    RUNTIME_HOST="${MATRIX_REAL_DB_RUNTIME_HOST}" \
    RUNTIME_PORT="${MATRIX_REAL_DB_RUNTIME_PORT}" \
    RUNTIME_ENDPOINT="$(matrix_real_db_runtime_endpoint)" \
    TARGET_FILE="${file}" \
    python3 - <<'PY'
import os
from pathlib import Path

target = Path(os.environ["TARGET_FILE"])
text = target.read_text(encoding="utf-8", errors="replace")
text = text.replace(os.environ["INSTALL_ENDPOINT"], os.environ["RUNTIME_ENDPOINT"])
text = text.replace(os.environ["INSTALL_HOST"], os.environ["RUNTIME_HOST"])
if os.environ["INSTALL_PORT"] != os.environ["RUNTIME_PORT"]:
    text = text.replace(os.environ["INSTALL_PORT"], os.environ["RUNTIME_PORT"])
target.write_text(text, encoding="utf-8")
PY
}

matrix_real_mark_managed() {
    local docroot="$1"
    mkdir -p "${docroot}/.apps-matrix"
    printf 'apps-matrix\n' >"${docroot}/.apps-matrix/managed"
}

matrix_real_safe_clean_dir() {
    local path="$1"
    local fixture_root="${MATRIX_FIXTURE_ROOT:-${MATRIX_OUT_DIR}/fixtures/docroots}"
    local source_root="${MATRIX_REAL_SOURCE_ROOT:-${MATRIX_OUT_DIR}/real-sources}"
    local resolved_path resolved_root
    resolved_path="$(realpath -m "${path}")"
    resolved_root="$(realpath -m "${fixture_root}")"

    if [[ ! -e "${resolved_path}" ]]; then
        return 0
    fi

    if [[ "${resolved_path}" == "${resolved_root}/"* ]]; then
        rm -rf "${resolved_path}"
        return 0
    fi

    resolved_root="$(realpath -m "${source_root}")"
    if [[ "${resolved_path}" == "${resolved_root}/"* ]]; then
        rm -rf "${resolved_path}"
        return 0
    fi

    if [[ "${MATRIX_FIXTURE_CLEAN:-0}" == "1" && -f "${resolved_path}/.apps-matrix/managed" ]]; then
        rm -rf "${resolved_path}"
        return 0
    fi

    matrix_fail "refusing to clean unmanaged docroot outside MATRIX_FIXTURE_ROOT: ${path}"
    return 1
}

matrix_real_log_file() {
    local scenario="$1"
    local name="$2"
    mkdir -p "${MATRIX_REAL_LOG_DIR}/${scenario}"
    printf '%s/%s/%s.log\n' "${MATRIX_REAL_LOG_DIR}" "${scenario}" "${name}"
}

matrix_real_print_command() {
    local redact_next=0
    local arg lower
    printf 'command:'
    for arg in "$@"; do
        if [[ "${redact_next}" == "1" ]]; then
            printf ' %q' "<redacted>"
            redact_next=0
            continue
        fi

        lower="${arg,,}"
        case "${lower}" in
            --dbpass|--db-pass|--database-pass|--admin-pass|--admin-password|--admin_password|--account-pass|--account-password|--pass)
                printf ' %q' "${arg}"
                redact_next=1
                ;;
            --dbpass=*|--db-pass=*|--database-pass=*|--admin-pass=*|--admin-password=*|--admin_password=*|--account-pass=*|--account-password=*|--pass=*)
                printf ' %q' "${arg%%=*}=<redacted>"
                ;;
            --db-url=mysql://*)
                printf ' %q' "--db-url=mysql://<redacted>"
                ;;
            -p*)
                printf ' %q' "-p<redacted>"
                ;;
            *)
                printf ' %q' "${arg}"
                ;;
        esac
    done
    printf '\n'
}

matrix_real_csv_append() {
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

matrix_real_init_observability() {
    local cache_csv="${MATRIX_OUT_DIR}/real-install-cache.csv"
    local steps_csv="${MATRIX_OUT_DIR}/real-install-steps.csv"
    local health_csv="${MATRIX_OUT_DIR}/real-install-health.csv"
    if [[ ! -s "${cache_csv}" ]]; then
        matrix_real_csv_append "${cache_csv}" scenario url dest status bytes
    fi
    if [[ ! -s "${steps_csv}" ]]; then
        matrix_real_csv_append "${steps_csv}" scenario step duration_ms result log_file
    fi
    if [[ ! -s "${health_csv}" ]]; then
        matrix_real_csv_append "${health_csv}" scenario probe result detail log_file
    fi
}

matrix_real_run_logged() {
    local scenario="$1"
    local name="$2"
    shift 2
    local log_file
    local started ended duration_ms
    log_file="$(matrix_real_log_file "${scenario}" "${name}")"
    matrix_banner "real install ${scenario}: ${name}"
    started="$(date +%s%3N)"
    {
        matrix_real_print_command "$@"
        "$@"
    } >"${log_file}" 2>&1 || {
        ended="$(date +%s%3N)"
        duration_ms=$((ended - started))
        matrix_real_csv_append "${MATRIX_OUT_DIR}/real-install-steps.csv" "${scenario}" "${name}" "${duration_ms}" failed "${log_file}"
        matrix_fail "real install step failed: ${scenario}/${name}; see ${log_file}"
        return 1
    }
    ended="$(date +%s%3N)"
    duration_ms=$((ended - started))
    matrix_real_csv_append "${MATRIX_OUT_DIR}/real-install-steps.csv" "${scenario}" "${name}" "${duration_ms}" ok "${log_file}"
}

matrix_real_run_in_dir_logged() {
    local scenario="$1"
    local name="$2"
    local dir="$3"
    shift 3
    local log_file
    local started ended duration_ms
    log_file="$(matrix_real_log_file "${scenario}" "${name}")"
    matrix_banner "real install ${scenario}: ${name}"
    started="$(date +%s%3N)"
    {
        printf 'cwd: %s\n' "${dir}"
        matrix_real_print_command "$@"
        (
            cd "${dir}"
            "$@"
        )
    } >"${log_file}" 2>&1 || {
        ended="$(date +%s%3N)"
        duration_ms=$((ended - started))
        matrix_real_csv_append "${MATRIX_OUT_DIR}/real-install-steps.csv" "${scenario}" "${name}" "${duration_ms}" failed "${log_file}"
        matrix_fail "real install step failed: ${scenario}/${name}; see ${log_file}"
        return 1
    }
    ended="$(date +%s%3N)"
    duration_ms=$((ended - started))
    matrix_real_csv_append "${MATRIX_OUT_DIR}/real-install-steps.csv" "${scenario}" "${name}" "${duration_ms}" ok "${log_file}"
}

matrix_real_download() {
    local url="$1"
    local dest="$2"
    local scenario="${3:-unknown}"
    local bytes
    mkdir -p "$(dirname "${dest}")"
    if [[ -s "${dest}" ]]; then
        bytes="$(wc -c <"${dest}" | tr -d ' ')"
        matrix_real_csv_append "${MATRIX_OUT_DIR}/real-install-cache.csv" "${scenario}" "${url}" "${dest}" hit "${bytes}"
        return 0
    fi
    matrix_real_require_tool curl
    curl -fL --retry 3 --retry-delay 2 -o "${dest}" "${url}"
    bytes="$(wc -c <"${dest}" | tr -d ' ')"
    matrix_real_csv_append "${MATRIX_OUT_DIR}/real-install-cache.csv" "${scenario}" "${url}" "${dest}" miss "${bytes}"
}

matrix_real_extract_tar_strip() {
    local archive="$1"
    local dest="$2"
    matrix_real_safe_clean_dir "${dest}"
    mkdir -p "${dest}"
    tar xf "${archive}" --strip-components=1 -C "${dest}"
}

matrix_real_extract_zip() {
    local archive="$1"
    local dest="$2"
    matrix_real_safe_clean_dir "${dest}"
    mkdir -p "${dest}"
    unzip -q "${archive}" -d "${dest}"
}

matrix_real_write_plan() {
    local scenario="$1"
    local app="$2"
    local plan_dir="${MATRIX_OUT_DIR}/real-install-plans"
    mkdir -p "${plan_dir}"
    SCENARIO="${scenario}" \
    APP="${app}" \
    MODE="real" \
    NETWORK_ENABLED="${MATRIX_REAL_ALLOW_NETWORK}" \
    APP_INSTALL_ENABLED="${MATRIX_REAL_RUN_APP_INSTALL}" \
    SOURCE_ROOT="${MATRIX_REAL_SOURCE_ROOT}" \
    DOWNLOAD_DIR="${MATRIX_REAL_DOWNLOAD_DIR}" \
    LOG_DIR="${MATRIX_REAL_LOG_DIR}" \
    PLAN_PATH="${plan_dir}/${scenario}.json" \
    python3 - <<'PY'
import json
import os

plan = {
    "scenario": os.environ["SCENARIO"],
    "app": os.environ["APP"],
    "mode": os.environ["MODE"],
    "network_enabled": os.environ["NETWORK_ENABLED"] == "1",
    "app_install_enabled": os.environ["APP_INSTALL_ENABLED"] == "1",
    "source_root": os.environ["SOURCE_ROOT"],
    "download_dir": os.environ["DOWNLOAD_DIR"],
    "log_dir": os.environ["LOG_DIR"],
}
with open(os.environ["PLAN_PATH"], "w", encoding="utf-8") as fh:
    json.dump(plan, fh, indent=2, sort_keys=True)
    fh.write("\n")
PY
}

matrix_real_prepare_source() {
    local scenario="$1"
    local source_dir="${MATRIX_REAL_SOURCE_ROOT}/${scenario}"
    matrix_real_safe_clean_dir "${source_dir}"
    mkdir -p "${source_dir}"
    printf '%s\n' "${source_dir}"
}

matrix_real_seed_common_php() {
    local docroot="$1"
    mkdir -p "${docroot}"
    if [[ ! -f "${docroot}/index.php" ]]; then
        cat >"${docroot}/index.php" <<'PHP'
<?php
header('Content-Type: text/html; charset=utf-8');
echo "apps-matrix real fixture\n";
PHP
    fi
}

matrix_real_seed_wordpress_htaccess() {
    local docroot="$1"
    local scenario="$2"
    mkdir -p "${docroot}/wp-content/cache-matrix" "${docroot}/wp-includes" "${docroot}/wp-content/uploads"
    printf 'wordpress css\n' > "${docroot}/wp-content/cache-matrix/test.css"
    printf 'wordpress js\n' > "${docroot}/wp-content/cache-matrix/test.js"
    printf 'lscache css\n' > "${docroot}/wp-content/cache-matrix/lscache.css"
    printf 'wp optimize css\n' > "${docroot}/wp-content/cache-matrix/wp-optimize.css"
    printf 'jpg fixture\n' > "${docroot}/wp-content/cache-matrix/test-image.jpg"
    mkdir -p "${docroot}/wp-content/cache-matrix/originals"
    touch "${docroot}/wp-content/cache-matrix/originals/.apps-matrix-keep"
    matrix_real_seed_wordpress_deny_index "${docroot}/wp-content/cache-matrix"
    matrix_real_seed_wordpress_deny_index "${docroot}/wp-includes"
    matrix_real_seed_wordpress_deny_index "${docroot}/wp-content/cache-matrix/originals"
    matrix_real_seed_wordpress_deny_index "${docroot}/wp-content/uploads"
    matrix_real_seed_wordpress_app_route_probe "${docroot}"
    printf 'deny\n' > "${docroot}/.user.ini"
    [[ -f "${docroot}/wp-config.php" ]] || printf "<?php\n// apps-matrix fixture\n" > "${docroot}/wp-config.php"

    cat >"${docroot}/.htaccess" <<'HTACCESS'
Options -Indexes
DirectoryIndex index.php index.html
<FilesMatch "(^\.|wp-config\.php|\.user\.ini)$">
  Require all denied
</FilesMatch>
<IfModule mod_headers.c>
  Header set X-Apps-Matrix "wordpress"
</IfModule>
RewriteEngine On
HTACCESS

    case "${scenario}" in
        wordpress-redirection)
            cat >>"${docroot}/.htaccess" <<'HTACCESS'
RewriteRule ^legacy-path/?$ /new-path/ [R=301,L]
RewriteRule ^campaign/(.*)$ /landing/$1 [R=301,L]
RewriteRule ^promo/?$ /offer [R=302,L]
RewriteRule ^removed-page/?$ - [G,L]
Redirect 301 /legacy-path/ /new-path/
RedirectMatch 301 ^/campaign/(.*)$ /landing/$1
Redirect 302 /promo /offer
Redirect gone /removed-page/
HTACCESS
            ;;
        wordpress-w3-total-cache|wordpress-litespeed-cache|wordpress-ewww|wordpress-wp-optimize)
            cat >>"${docroot}/.htaccess" <<HTACCESS
<IfModule mod_expires.c>
  ExpiresActive On
  ExpiresDefault "access plus 1 year"
</IfModule>
<IfModule mod_headers.c>
  Header set Cache-Control "public, max-age=31536000"
  Header append Vary "Accept-Encoding"
</IfModule>
HTACCESS
            ;;
    esac

    cat >>"${docroot}/.htaccess" <<'HTACCESS'
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule . /index.php [L]
HTACCESS
}

matrix_real_seed_wordpress_deny_index() {
    local dir="$1"
    mkdir -p "${dir}"
    cat >"${dir}/index.php" <<'PHP'
<?php
http_response_code(403);
header('Content-Type: text/plain; charset=utf-8');
echo "apps-matrix directory listing disabled\n";
PHP
}

matrix_real_seed_wordpress_app_route_probe() {
    local docroot="$1"
    local mu_dir="${docroot}/wp-content/mu-plugins"
    mkdir -p "${mu_dir}"
    cat >"${mu_dir}/apps-matrix-route-probe.php" <<'PHP'
<?php
/**
 * Plugin Name: Apps Matrix Route Probe
 */

add_action(
    'template_redirect',
    static function () {
        if (($_GET['__apps_matrix_probe'] ?? '') !== 'router') {
            return;
        }

        header('Content-Type: application/json; charset=utf-8');
        header('X-Probe-Marker: APPS_MATRIX_V1');
        echo json_encode(
            [
                'PROBE_MARKER'    => 'APPS_MATRIX_V1',
                'REQUEST_URI'     => $_SERVER['REQUEST_URI'] ?? null,
                'SCRIPT_NAME'     => $_SERVER['SCRIPT_NAME'] ?? null,
                'SCRIPT_FILENAME' => $_SERVER['SCRIPT_FILENAME'] ?? null,
                'PHP_SELF'        => $_SERVER['PHP_SELF'] ?? null,
                'PATH_INFO'       => $_SERVER['PATH_INFO'] ?? null,
                'REDIRECT_URL'    => $_SERVER['REDIRECT_URL'] ?? null,
                'REDIRECT_STATUS' => $_SERVER['REDIRECT_STATUS'] ?? null,
            ],
            JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES
        );
        exit;
    },
    0
);
PHP
}

matrix_real_install_wp_cli() {
    local bin="${MATRIX_REAL_DOWNLOAD_DIR}/wp-cli.phar"
    if [[ ! -s "${bin}" ]]; then
        matrix_real_download "https://raw.githubusercontent.com/wp-cli/builds/gh-pages/phar/wp-cli.phar" "${bin}" "wp-cli"
        chmod +x "${bin}"
    fi
    printf '%s\n' "${bin}"
}

matrix_real_default_strict_plugin_install() {
    case "$1" in
        wordpress-w3-total-cache|wordpress-litespeed-cache|wordpress-wordfence|wordpress-redirection|wordpress-ewww|wordpress-wp-optimize)
            printf '1\n'
            ;;
        *)
            printf '0\n'
            ;;
    esac
}

matrix_real_effective_strict_plugin_install() {
    local scenario="$1"
    if [[ -n "${MATRIX_REAL_STRICT_PLUGIN_INSTALL_OVERRIDE:-}" ]]; then
        printf '%s\n' "${MATRIX_REAL_STRICT_PLUGIN_INSTALL_OVERRIDE}"
        return
    fi
    if [[ "${MATRIX_REAL_STRICT_PLUGIN_INSTALL:-auto}" == "auto" ]]; then
        matrix_real_default_strict_plugin_install "${scenario}"
    else
        printf '%s\n' "${MATRIX_REAL_STRICT_PLUGIN_INSTALL}"
    fi
}

matrix_real_wp_plugin_for_scenario() {
    case "$1" in
        wordpress-w3-total-cache) printf '%s\n' "${MATRIX_PLUGIN_W3TC}" ;;
        wordpress-litespeed-cache) printf '%s\n' "${MATRIX_PLUGIN_LSCACHE}" ;;
        wordpress-wordfence) printf '%s\n' "${MATRIX_PLUGIN_WORDFENCE}" ;;
        wordpress-redirection) printf '%s\n' "${MATRIX_PLUGIN_REDIRECTION}" ;;
        wordpress-ewww) printf '%s\n' "${MATRIX_PLUGIN_EWWW}" ;;
        wordpress-wp-optimize) printf '%s\n' "${MATRIX_PLUGIN_WP_OPTIMIZE}" ;;
        *) return 1 ;;
    esac
}

matrix_real_configure_wordpress_plugin() {
    local scenario="$1"
    local source_dir="$2"
    local wp="$3"
    case "${scenario}" in
        wordpress-redirection)
            matrix_real_run_logged "${scenario}" wp-redirection-options php "${wp}" --path="${source_dir}" option update redirection_options '{"monitor_post":1,"expire_redirect":7}' --format=json --allow-root || true
            ;;
        wordpress-w3-total-cache)
            mkdir -p "${source_dir}/wp-content/cache" "${source_dir}/wp-content/w3tc-config"
            matrix_real_run_logged "${scenario}" wp-w3tc-options php "${wp}" --path="${source_dir}" option update w3tc_config '{"browsercache.enabled":true,"pgcache.enabled":true}' --format=json --allow-root || true
            ;;
        wordpress-litespeed-cache)
            matrix_real_run_logged "${scenario}" wp-lscache-options php "${wp}" --path="${source_dir}" option update litespeed.conf.optm-css_min 1 --allow-root || true
            matrix_real_run_logged "${scenario}" wp-lscache-cache-options php "${wp}" --path="${source_dir}" option update litespeed.conf.cache 1 --allow-root || true
            ;;
        wordpress-wordfence)
            matrix_real_run_logged "${scenario}" wp-wordfence-options php "${wp}" --path="${source_dir}" option update wordfenceActivated 1 --allow-root || true
            ;;
        wordpress-ewww)
            matrix_real_run_logged "${scenario}" wp-ewww-options php "${wp}" --path="${source_dir}" option update ewww_image_optimizer_jpg_quality 82 --allow-root || true
            ;;
        wordpress-wp-optimize)
            matrix_real_run_logged "${scenario}" wp-optimize-options php "${wp}" --path="${source_dir}" option update wpo_cache_config '{"enable_page_caching":true,"enable_browser_cache":true}' --format=json --allow-root || true
            ;;
    esac
}

matrix_real_write_wordpress_plugin_config_snapshot() {
    local scenario="$1"
    local source_dir="$2"
    local wp="$3"
    local target="${source_dir}/.apps-matrix/wordpress_plugin_config.json"
    mkdir -p "$(dirname "${target}")"
    SCENARIO="${scenario}" TARGET="${target}" php "${wp}" --path="${source_dir}" option list --format=json --allow-root >"${target}.tmp" 2>/dev/null || {
        SCENARIO="${scenario}" TARGET="${target}" python3 - <<'PY'
import json
import os

with open(os.environ["TARGET"], "w", encoding="utf-8") as fh:
    json.dump({"scenario": os.environ["SCENARIO"], "options": [], "snapshot_error": True}, fh, indent=2)
    fh.write("\n")
PY
        return 0
    }
    SCENARIO="${scenario}" SOURCE="${target}.tmp" TARGET="${target}" python3 - <<'PY'
import json
import os
from pathlib import Path

source = Path(os.environ["SOURCE"])
target = Path(os.environ["TARGET"])
try:
    options = json.loads(source.read_text(encoding="utf-8"))
except json.JSONDecodeError:
    options = []
target.write_text(
    json.dumps({"scenario": os.environ["SCENARIO"], "options": options}, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
source.unlink(missing_ok=True)
PY
}

matrix_real_record_health() {
    local scenario="$1"
    local probe="$2"
    local result="$3"
    local detail="$4"
    local log_file="$5"
    local target="${MATRIX_OUT_DIR}/real-install-health/${scenario}/${probe}.json"
    mkdir -p "$(dirname "${target}")"
    SCENARIO="${scenario}" PROBE="${probe}" RESULT="${result}" DETAIL="${detail}" LOG_FILE="${log_file}" TARGET="${target}" python3 - <<'PY'
import json
import os

payload = {
    "scenario": os.environ["SCENARIO"],
    "probe": os.environ["PROBE"],
    "result": os.environ["RESULT"],
    "detail": os.environ["DETAIL"],
    "log_file": os.environ["LOG_FILE"],
}
with open(os.environ["TARGET"], "w", encoding="utf-8") as fh:
    json.dump(payload, fh, indent=2, sort_keys=True)
    fh.write("\n")
PY
    matrix_real_csv_append "${MATRIX_OUT_DIR}/real-install-health.csv" "${scenario}" "${probe}" "${result}" "${detail}" "${log_file}"
}

matrix_real_run_health_probe() {
    local scenario="$1"
    local probe="$2"
    local detail="$3"
    shift 3
    local log_file
    log_file="$(matrix_real_log_file "${scenario}" "health-${probe}")"
    {
        matrix_real_print_command "$@"
        "$@"
    } >"${log_file}" 2>&1 || {
        matrix_real_record_health "${scenario}" "${probe}" failed "${detail}" "${log_file}"
        if [[ "${MATRIX_REAL_ENFORCE_HEALTH_PROBES}" == "1" ]]; then
            matrix_fail "real install health probe failed: ${scenario}/${probe}; see ${log_file}"
            return 1
        fi
        matrix_warn "real install health probe failed but enforcement is disabled: ${scenario}/${probe}; see ${log_file}"
        return 0
    }
    matrix_real_record_health "${scenario}" "${probe}" ok "${detail}" "${log_file}"
}

matrix_real_run_health_probe_in_dir() {
    local scenario="$1"
    local probe="$2"
    local detail="$3"
    local dir="$4"
    shift 4
    local log_file
    log_file="$(matrix_real_log_file "${scenario}" "health-${probe}")"
    {
        printf 'cwd: %s\n' "${dir}"
        matrix_real_print_command "$@"
        (
            cd "${dir}"
            "$@"
        )
    } >"${log_file}" 2>&1 || {
        matrix_real_record_health "${scenario}" "${probe}" failed "${detail}" "${log_file}"
        if [[ "${MATRIX_REAL_ENFORCE_HEALTH_PROBES}" == "1" ]]; then
            matrix_fail "real install health probe failed: ${scenario}/${probe}; see ${log_file}"
            return 1
        fi
        matrix_warn "real install health probe failed but enforcement is disabled: ${scenario}/${probe}; see ${log_file}"
        return 0
    }
    matrix_real_record_health "${scenario}" "${probe}" ok "${detail}" "${log_file}"
}

matrix_real_health_probe_wordpress() {
    local scenario="$1"
    local source_dir="$2"
    [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]] || return 0
    local wp
    wp="$(matrix_real_install_wp_cli)"
    matrix_real_run_health_probe "${scenario}" wordpress-core "wp core is-installed" \
        php "${wp}" --path="${source_dir}" core is-installed --allow-root
    matrix_real_run_health_probe "${scenario}" wordpress-option "wp option home" \
        php "${wp}" --path="${source_dir}" option get home --allow-root
    if matrix_real_wp_plugin_for_scenario "${scenario}" >/dev/null 2>&1; then
        local plugin
        plugin="$(matrix_real_wp_plugin_for_scenario "${scenario}")"
        matrix_real_run_health_probe "${scenario}" wordpress-plugin "wp plugin is-installed ${plugin}" \
            php "${wp}" --path="${source_dir}" plugin is-installed "${plugin}" --allow-root
    fi
}

matrix_real_health_probe_laravel() {
    local scenario="$1"
    local source_dir="$2"
    [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]] || return 0
    matrix_real_run_health_probe_in_dir "${scenario}" laravel-about "php artisan about" \
        "${source_dir}" php artisan about --no-interaction
}

matrix_real_health_probe_drupal() {
    local scenario="$1"
    local source_dir="$2"
    [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]] || return 0
    if [[ -x "${source_dir}/vendor/bin/drush" ]]; then
        matrix_real_run_health_probe_in_dir "${scenario}" drupal-status "drush status" \
            "${source_dir}" env COMPOSER_ALLOW_SUPERUSER=1 php vendor/drush/drush/drush --root=web \
            --uri="${MATRIX_REAL_BASE_URL:-http://localhost}" status
    fi
}

matrix_real_health_probe_nextcloud() {
    local scenario="$1"
    local source_dir="$2"
    [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]] || return 0
    matrix_real_run_health_probe "${scenario}" nextcloud-status "occ status" \
        php "${source_dir}/occ" status
}

matrix_real_health_probe_joomla() {
    local scenario="$1"
    local source_dir="$2"
    [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]] || return 0
    matrix_real_run_health_probe "${scenario}" joomla-cli-list "joomla cli list" \
        php "${source_dir}/cli/joomla.php" list
}

matrix_real_health_probe_mediawiki() {
    local scenario="$1"
    local source_dir="$2"
    [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]] || return 0
    matrix_real_run_health_probe "${scenario}" mediawiki-version "maintenance showSiteStats" \
        php "${source_dir}/maintenance/showSiteStats.php"
}

matrix_real_run_health_probes() {
    local scenario="$1"
    local source_dir="$2"
    if [[ "${MATRIX_REAL_HEALTH_PROBES}" != "1" ]]; then
        return 0
    fi
    case "${scenario}" in
        wordpress-*) matrix_real_health_probe_wordpress "${scenario}" "${source_dir}" ;;
        laravel) matrix_real_health_probe_laravel "${scenario}" "${source_dir}" ;;
        drupal) matrix_real_health_probe_drupal "${scenario}" "${source_dir}" ;;
        nextcloud) matrix_real_health_probe_nextcloud "${scenario}" "${source_dir}" ;;
        joomla) matrix_real_health_probe_joomla "${scenario}" "${source_dir}" ;;
        mediawiki) matrix_real_health_probe_mediawiki "${scenario}" "${source_dir}" ;;
    esac
}

matrix_real_install_wordpress() {
    local scenario="$1"
    local source_dir="$2"
    local archive="${MATRIX_REAL_DOWNLOAD_DIR}/wordpress-${MATRIX_WORDPRESS_VERSION}.tar.gz"
    matrix_real_download "https://wordpress.org/wordpress-${MATRIX_WORDPRESS_VERSION}.tar.gz" "${archive}" "${scenario}"
    matrix_real_extract_tar_strip "${archive}" "${source_dir}"
    matrix_real_seed_wordpress_htaccess "${source_dir}" "${scenario}"

    if [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]]; then
        local wp plugin db_name
        wp="$(matrix_real_install_wp_cli)"
        db_name="$(matrix_real_db_name "${scenario}")"
        matrix_real_create_database "${db_name}"
        matrix_real_run_logged "${scenario}" wp-config php "${wp}" --path="${source_dir}" config create \
            --dbname="${db_name}" \
            --dbuser="${MATRIX_REAL_DB_ROOT_USER}" \
            --dbpass="${MATRIX_REAL_DB_ROOT_PASSWORD}" \
            --dbhost="$(matrix_real_db_install_endpoint)" \
            --skip-check --force --allow-root
        matrix_real_run_logged "${scenario}" wp-core-install php "${wp}" --path="${source_dir}" core install \
            --url="${MATRIX_REAL_BASE_URL:-http://localhost}" \
            --title="apps-matrix ${scenario}" \
            --admin_user="${MATRIX_WORDPRESS_ADMIN_USER}" \
            --admin_password="${MATRIX_WORDPRESS_ADMIN_PASSWORD}" \
            --admin_email="${MATRIX_WORDPRESS_ADMIN_EMAIL}" \
            --skip-email --allow-root
        matrix_real_run_logged "${scenario}" wp-permalinks php "${wp}" --path="${source_dir}" rewrite structure '/%postname%/' --hard --allow-root
        if plugin="$(matrix_real_wp_plugin_for_scenario "${scenario}")"; then
            matrix_real_run_logged "${scenario}" wp-plugin php "${wp}" --path="${source_dir}" plugin install "${plugin}" --activate --allow-root || true
            if [[ "${MATRIX_REAL_DEEP_CONFIG}" == "1" ]]; then
                matrix_real_configure_wordpress_plugin "${scenario}" "${source_dir}" "${wp}"
                matrix_real_write_wordpress_plugin_config_snapshot "${scenario}" "${source_dir}" "${wp}"
            fi
        fi
        matrix_real_run_health_probes "${scenario}" "${source_dir}"
        matrix_real_run_logged "${scenario}" wp-runtime-db-host php "${wp}" --path="${source_dir}" config set DB_HOST "$(matrix_real_db_runtime_endpoint)" --type=constant --allow-root
    fi
}

matrix_real_write_laravel_env() {
    local scenario="$1"
    local source_dir="$2"
    local db_host="$3"
    local db_port="$4"
    local app_key=""
    if [[ -f "${source_dir}/.env" ]]; then
        app_key="$(awk '/^APP_KEY=/{sub(/^APP_KEY=/, ""); print; exit}' "${source_dir}/.env")"
    fi
    cat >"${source_dir}/.env" <<EOF
APP_ENV=testing
APP_DEBUG=false
APP_KEY=${app_key}
APP_URL=${MATRIX_REAL_BASE_URL:-http://localhost}
DB_CONNECTION=mysql
DB_HOST=${db_host}
DB_PORT=${db_port}
DB_DATABASE=$(matrix_real_db_name "${scenario}")
DB_USERNAME=${MATRIX_REAL_DB_ROOT_USER}
DB_PASSWORD=${MATRIX_REAL_DB_ROOT_PASSWORD}
EOF
}

matrix_real_write_laravel_config_snapshot() {
    local scenario="$1"
    local source_dir="$2"
    local target="${source_dir}/public/.apps-matrix/laravel_config.json"
    mkdir -p "$(dirname "${target}")"
    SCENARIO="${scenario}" SOURCE_ENV="${source_dir}/.env" TARGET="${target}" python3 - <<'PY'
import json
import os
from pathlib import Path

values = {}
source = Path(os.environ["SOURCE_ENV"])
for line in source.read_text(encoding="utf-8", errors="replace").splitlines():
    if not line or line.startswith("#") or "=" not in line:
        continue
    key, value = line.split("=", 1)
    values[key] = value

safe_keys = (
    "APP_ENV",
    "APP_DEBUG",
    "APP_URL",
    "DB_CONNECTION",
    "DB_HOST",
    "DB_PORT",
    "DB_DATABASE",
    "DB_USERNAME",
)
payload = {
    "scenario": os.environ["SCENARIO"],
    "config": {key: values.get(key, "") for key in safe_keys},
    "db_password_configured": bool(values.get("DB_PASSWORD")),
}
Path(os.environ["TARGET"]).write_text(
    json.dumps(payload, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY
}

matrix_real_seed_laravel_routes() {
    local source_dir="$1"
    cat >>"${source_dir}/routes/web.php" <<'PHP'

\Illuminate\Support\Facades\Route::get('/up', function () {
    if (request()->query('__apps_matrix_probe') === 'router') {
        return response()->json([
            'PROBE_MARKER'    => 'APPS_MATRIX_V1',
            'REQUEST_URI'     => request()->server('REQUEST_URI'),
            'SCRIPT_NAME'     => '/index.php',
            'SCRIPT_FILENAME' => request()->server('SCRIPT_FILENAME'),
            'PHP_SELF'        => request()->server('PHP_SELF'),
            'PATH_INFO'       => request()->server('PATH_INFO'),
            'REDIRECT_URL'    => request()->server('REDIRECT_URL'),
            'REDIRECT_STATUS' => request()->server('REDIRECT_STATUS'),
        ]);
    }

    return response('OK', 200);
});
PHP
}

matrix_real_write_drupal_semantic_snapshot() {
    local scenario="$1"
    local source_dir="$2"
    local target="${source_dir}/web/.apps-matrix/drupal_semantic.json"
    mkdir -p "$(dirname "${target}")"
    SCENARIO="${scenario}" \
    EXPECTED_DB="$(matrix_real_db_name "${scenario}")" \
    SETTINGS="${source_dir}/web/sites/default/settings.php" \
    DRUSH="${source_dir}/vendor/bin/drush" \
    TARGET="${target}" \
    python3 - <<'PY'
import json
import os
import re
from pathlib import Path

settings = Path(os.environ["SETTINGS"])
text = settings.read_text(encoding="utf-8", errors="replace") if settings.exists() else ""
expected = os.environ["EXPECTED_DB"]
database_matches = re.findall(r"['\"]database['\"]\s*=>\s*['\"]([^'\"]+)['\"]", text)
database_name = next((value for value in database_matches if value == expected), "")
if not database_name and database_matches:
    database_name = database_matches[-1]
payload = {
    "scenario": os.environ["SCENARIO"],
    "expected_database_name": expected,
    "database_name": database_name,
    "settings_php_present": settings.is_file(),
    "drush_present": Path(os.environ["DRUSH"]).is_file(),
}
Path(os.environ["TARGET"]).write_text(
    json.dumps(payload, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY
}

matrix_real_write_nextcloud_semantic_snapshot() {
    local scenario="$1"
    local source_dir="$2"
    local target="${source_dir}/.apps-matrix/nextcloud_semantic.json"
    mkdir -p "$(dirname "${target}")"
    SCENARIO="${scenario}" \
    EXPECTED_DB="$(matrix_real_db_name "${scenario}")" \
    CONFIG="${source_dir}/config/config.php" \
    TARGET="${target}" \
    python3 - <<'PY'
import json
import os
import re
from pathlib import Path

config = Path(os.environ["CONFIG"])
text = config.read_text(encoding="utf-8", errors="replace") if config.exists() else ""
database_match = re.search(r"['\"]dbname['\"]\s*=>\s*['\"]([^'\"]+)['\"]", text)
installed = bool(re.search(r"['\"]installed['\"]\s*=>\s*true", text, re.IGNORECASE))
payload = {
    "scenario": os.environ["SCENARIO"],
    "expected_database_name": os.environ["EXPECTED_DB"],
    "database_name": database_match.group(1) if database_match else "",
    "config_php_present": config.is_file(),
    "installed": installed,
}
Path(os.environ["TARGET"]).write_text(
    json.dumps(payload, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY
}

matrix_real_rewrite_nextcloud_runtime_config() {
    local config_file="$1"
    [[ -f "${config_file}" ]] || return 0
    CONFIG_FILE="${config_file}" python3 - <<'PY'
import re
import os
from pathlib import Path

path = Path(os.environ["CONFIG_FILE"])
text = path.read_text(encoding="utf-8", errors="replace")
text = re.sub(
    r"(['\"]datadirectory['\"]\s*=>\s*)(['\"])(.*?)\2",
    r"\1__DIR__ . '/../data'",
    text,
    count=1,
)
path.write_text(text, encoding="utf-8")
PY
}

matrix_real_write_joomla_semantic_snapshot() {
    local scenario="$1"
    local source_dir="$2"
    local target="${source_dir}/.apps-matrix/joomla_semantic.json"
    mkdir -p "$(dirname "${target}")"
    SCENARIO="${scenario}" \
    EXPECTED_DB="$(matrix_real_db_name "${scenario}")" \
    CONFIG="${source_dir}/configuration.php" \
    TARGET="${target}" \
    python3 - <<'PY'
import json
import os
import re
from pathlib import Path

config = Path(os.environ["CONFIG"])
text = config.read_text(encoding="utf-8", errors="replace") if config.exists() else ""

def property_value(name: str) -> str:
    match = re.search(r"public\s+\$" + re.escape(name) + r"\s*=\s*['\"]([^'\"]*)['\"]", text)
    return match.group(1) if match else ""

payload = {
    "scenario": os.environ["SCENARIO"],
    "expected_database_name": os.environ["EXPECTED_DB"],
    "database_name": property_value("db"),
    "db_type": property_value("dbtype"),
    "config_php_present": config.is_file(),
}
Path(os.environ["TARGET"]).write_text(
    json.dumps(payload, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY
}

matrix_real_write_mediawiki_semantic_snapshot() {
    local scenario="$1"
    local source_dir="$2"
    local target="${source_dir}/.apps-matrix/mediawiki_semantic.json"
    mkdir -p "$(dirname "${target}")"
    SCENARIO="${scenario}" \
    EXPECTED_DB="$(matrix_real_db_name "${scenario}")" \
    LOCAL_SETTINGS="${source_dir}/LocalSettings.php" \
    TARGET="${target}" \
    python3 - <<'PY'
import json
import os
import re
from pathlib import Path

settings = Path(os.environ["LOCAL_SETTINGS"])
text = settings.read_text(encoding="utf-8", errors="replace") if settings.exists() else ""

def variable_value(name: str) -> str:
    match = re.search(r"\$" + re.escape(name) + r"\s*=\s*['\"]([^'\"]*)['\"]", text)
    return match.group(1) if match else ""

payload = {
    "scenario": os.environ["SCENARIO"],
    "expected_database_name": os.environ["EXPECTED_DB"],
    "database_name": variable_value("wgDBname"),
    "site_name": variable_value("wgSitename"),
    "local_settings_present": settings.is_file(),
}
Path(os.environ["TARGET"]).write_text(
    json.dumps(payload, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY
}

matrix_real_install_laravel() {
    local scenario="$1"
    local source_dir="$2"
    matrix_real_require_tool composer
    matrix_real_run_logged "${scenario}" composer-create-project composer create-project --no-interaction --prefer-dist "laravel/laravel:${MATRIX_LARAVEL_VERSION}" "${source_dir}"
    mkdir -p "${source_dir}/public"
    printf 'favicon\n' > "${source_dir}/public/favicon.ico"
    if [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]]; then
        matrix_real_write_laravel_env "${scenario}" "${source_dir}" "${MATRIX_REAL_DB_INSTALL_HOST}" "${MATRIX_REAL_DB_INSTALL_PORT}"
    else
        matrix_real_write_laravel_env "${scenario}" "${source_dir}" "${MATRIX_REAL_DB_RUNTIME_HOST}" "${MATRIX_REAL_DB_RUNTIME_PORT}"
    fi
    printf 'APP_ENV=testing\n' > "${source_dir}/public/.env"
    cat >>"${source_dir}/public/.htaccess" <<'HTACCESS'
<Files ".env">
  Require all denied
</Files>
HTACCESS
    if [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]]; then
        matrix_real_create_database "$(matrix_real_db_name "${scenario}")"
        matrix_real_seed_laravel_routes "${source_dir}"
        matrix_real_run_in_dir_logged "${scenario}" laravel-key "${source_dir}" php artisan key:generate --force
        if [[ -d "${source_dir}/database/migrations" ]]; then
            matrix_real_run_in_dir_logged "${scenario}" laravel-migrate "${source_dir}" php artisan migrate --force || true
        fi
        matrix_real_run_health_probes "${scenario}" "${source_dir}"
        matrix_real_write_laravel_env "${scenario}" "${source_dir}" "${MATRIX_REAL_DB_RUNTIME_HOST}" "${MATRIX_REAL_DB_RUNTIME_PORT}"
    fi
    matrix_real_write_laravel_config_snapshot "${scenario}" "${source_dir}"
}

matrix_real_install_drupal() {
    local scenario="$1"
    local source_dir="$2"
    matrix_real_require_tool composer
    matrix_real_run_logged "${scenario}" composer-create-project \
        env COMPOSER_ALLOW_SUPERUSER=1 composer create-project --no-interaction --prefer-dist "drupal/recommended-project:${MATRIX_DRUPAL_VERSION}" "${source_dir}"
    mkdir -p "${source_dir}/web/sites/default/files" "${source_dir}/web/core/misc"
    printf 'drupal js\n' > "${source_dir}/web/core/misc/drupal.js"
    if [[ "${MATRIX_REAL_RUN_APP_INSTALL}" != "1" ]]; then
        printf "<?php\n// apps-matrix\n" > "${source_dir}/web/sites/default/settings.php"
    fi
    cat >"${source_dir}/web/.htaccess" <<'HTACCESS'
Options -Indexes
<Files "settings.php">
  Require all denied
</Files>
RewriteEngine On
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule . /index.php [L]
HTACCESS
    if [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]]; then
        local db_name
        db_name="$(matrix_real_db_name "${scenario}")"
        matrix_real_create_database "${db_name}"
        matrix_real_run_in_dir_logged "${scenario}" composer-drush "${source_dir}" \
            env COMPOSER_ALLOW_SUPERUSER=1 composer require --dev --no-interaction "drush/drush:${MATRIX_DRUSH_VERSION}"
        chmod 755 "${source_dir}/web/sites/default"
        matrix_real_run_in_dir_logged "${scenario}" drupal-site-install "${source_dir}" \
            env COMPOSER_ALLOW_SUPERUSER=1 php vendor/drush/drush/drush --root=web --uri="${MATRIX_REAL_BASE_URL:-http://localhost}" site:install standard \
            "--db-url=mysql://${MATRIX_REAL_DB_ROOT_USER}:${MATRIX_REAL_DB_ROOT_PASSWORD}@$(matrix_real_db_install_endpoint)/${db_name}" \
            "--site-name=Apps Matrix" \
            "--account-name=${MATRIX_WORDPRESS_ADMIN_USER}" \
            "--account-pass=${MATRIX_WORDPRESS_ADMIN_PASSWORD}" \
            -y
        matrix_real_run_health_probes "${scenario}" "${source_dir}"
        matrix_real_rewrite_runtime_db_endpoint "${source_dir}/web/sites/default/settings.php"
        matrix_real_write_drupal_semantic_snapshot "${scenario}" "${source_dir}"
    fi
}

matrix_real_install_nextcloud() {
    local scenario="$1"
    local source_dir="$2"
    local archive="${MATRIX_REAL_DOWNLOAD_DIR}/nextcloud-${MATRIX_NEXTCLOUD_VERSION}.tar.bz2"
    matrix_real_download "https://download.nextcloud.com/server/releases/nextcloud-${MATRIX_NEXTCLOUD_VERSION}.tar.bz2" "${archive}" "${scenario}"
    matrix_real_extract_tar_strip "${archive}" "${source_dir}"
    mkdir -p "${source_dir}/config" "${source_dir}/data"
    if [[ "${MATRIX_REAL_RUN_APP_INSTALL}" != "1" ]]; then
        printf "<?php\n// apps-matrix\n" > "${source_dir}/config/config.php"
    fi
    cat >"${source_dir}/.htaccess" <<'HTACCESS'
Options -Indexes
<Files "config.php">
  Require all denied
</Files>
RedirectMatch 403 ^/data/
RewriteEngine On
RewriteRule ^data/?$ - [F,L]
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule ^ /index.php [L]
HTACCESS
    if [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]]; then
        local db_name
        db_name="$(matrix_real_db_name "${scenario}")"
        matrix_real_create_database "${db_name}"
        matrix_real_run_logged "${scenario}" nextcloud-maintenance-install php "${source_dir}/occ" maintenance:install \
            --database mysql \
            --database-name "${db_name}" \
            --database-host "$(matrix_real_db_install_endpoint)" \
            --database-user "${MATRIX_REAL_DB_ROOT_USER}" \
            --database-pass "${MATRIX_REAL_DB_ROOT_PASSWORD}" \
            --admin-user "${MATRIX_WORDPRESS_ADMIN_USER}" \
            --admin-pass "${MATRIX_WORDPRESS_ADMIN_PASSWORD}" \
            --data-dir "${source_dir}/data"
        matrix_real_run_logged "${scenario}" nextcloud-maintenance-off php "${source_dir}/occ" maintenance:mode --off
        matrix_real_run_logged "${scenario}" nextcloud-trusted-domain-localhost php "${source_dir}/occ" config:system:set trusted_domains 0 --value=localhost
        matrix_real_run_logged "${scenario}" nextcloud-trusted-domain-loopback php "${source_dir}/occ" config:system:set trusted_domains 1 --value=127.0.0.1
        matrix_real_run_logged "${scenario}" nextcloud-overwrite-cli-url php "${source_dir}/occ" config:system:set overwrite.cli.url --value="${MATRIX_REAL_BASE_URL:-http://localhost}"
        matrix_real_run_health_probes "${scenario}" "${source_dir}"
        matrix_real_rewrite_runtime_db_endpoint "${source_dir}/config/config.php"
        matrix_real_rewrite_nextcloud_runtime_config "${source_dir}/config/config.php"
        matrix_real_write_nextcloud_semantic_snapshot "${scenario}" "${source_dir}"
    fi
}

matrix_real_install_joomla() {
    local scenario="$1"
    local source_dir="$2"
    local archive="${MATRIX_REAL_DOWNLOAD_DIR}/joomla-${MATRIX_JOOMLA_VERSION}.zip"
    local joomla_version_path="${MATRIX_JOOMLA_VERSION//./-}"
    matrix_real_download "https://downloads.joomla.org/cms/joomla5/${joomla_version_path}/Joomla_${MATRIX_JOOMLA_VERSION}-Stable-Full_Package.zip?format=zip" "${archive}" "${scenario}"
    matrix_real_extract_zip "${archive}" "${source_dir}"
    mkdir -p "${source_dir}/media/system/js" "${source_dir}/administrator/cache"
    rm -f "${source_dir}/administrator/cache/index.html"
    cat >"${source_dir}/administrator/cache/.htaccess" <<'HTACCESS'
RewriteEngine On
RewriteRule ^ - [F,L]
HTACCESS
    printf 'joomla js\n' > "${source_dir}/media/system/js/core.js"
    if [[ "${MATRIX_REAL_RUN_APP_INSTALL}" != "1" ]]; then
        printf "<?php\n// apps-matrix\n" > "${source_dir}/configuration.php"
    fi
    cat >"${source_dir}/.htaccess" <<'HTACCESS'
Options -Indexes
<Files "configuration.php">
  Require all denied
</Files>
RewriteEngine On
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule . /index.php [L]
HTACCESS
    if [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]]; then
        local db_name
        db_name="$(matrix_real_db_name "${scenario}")"
        matrix_real_create_database "${db_name}"
        matrix_real_run_logged "${scenario}" joomla-cli-install php "${source_dir}/installation/joomla.php" install \
            --site-name "Apps Matrix" \
            --admin-user "${MATRIX_WORDPRESS_ADMIN_USER}" \
            --admin-username "${MATRIX_WORDPRESS_ADMIN_USER}" \
            --admin-password "${MATRIX_WORDPRESS_ADMIN_PASSWORD}" \
            --admin-email "${MATRIX_WORDPRESS_ADMIN_EMAIL}" \
            --db-type mysqli \
            --db-host "$(matrix_real_db_install_endpoint)" \
            --db-user "${MATRIX_REAL_DB_ROOT_USER}" \
            --db-pass "${MATRIX_REAL_DB_ROOT_PASSWORD}" \
            --db-name "${db_name}" \
            --no-interaction
        matrix_real_run_health_probes "${scenario}" "${source_dir}"
        matrix_real_rewrite_runtime_db_endpoint "${source_dir}/configuration.php"
        matrix_real_write_joomla_semantic_snapshot "${scenario}" "${source_dir}"
    fi
}

matrix_real_install_mediawiki() {
    local scenario="$1"
    local source_dir="$2"
    local archive="${MATRIX_REAL_DOWNLOAD_DIR}/mediawiki-${MATRIX_MEDIAWIKI_VERSION}.tar.gz"
    matrix_real_download "https://releases.wikimedia.org/mediawiki/${MATRIX_MEDIAWIKI_MAJOR}/mediawiki-${MATRIX_MEDIAWIKI_VERSION}.tar.gz" "${archive}" "${scenario}"
    matrix_real_extract_tar_strip "${archive}" "${source_dir}"
    mkdir -p "${source_dir}/resources/assets" "${source_dir}/maintenance" "${source_dir}/includes"
    printf 'png fixture\n' > "${source_dir}/resources/assets/wiki.png"
    printf "<?php\n// apps-matrix\n" > "${source_dir}/maintenance/run.php"
    cat >"${source_dir}/.htaccess" <<'HTACCESS'
Options -Indexes
<Files "run.php">
  Require all denied
</Files>
<IfModule mod_headers.c>
  Header unset Cache-Control
  Header set Cache-Control "public, max-age=86400"
</IfModule>
RewriteEngine On
RewriteRule ^$ /index.php/Main_Page [L,QSA]
RewriteRule ^wiki/(.*)$ /index.php/$1 [L,QSA]
HTACCESS
    if [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]]; then
        local db_name
        db_name="$(matrix_real_db_name "${scenario}")"
        matrix_real_create_database "${db_name}"
        matrix_real_run_logged "${scenario}" mediawiki-install php "${source_dir}/maintenance/install.php" \
            --dbtype mysql \
            --dbserver "$(matrix_real_db_install_endpoint)" \
            --dbname "${db_name}" \
            --dbuser "${MATRIX_REAL_DB_ROOT_USER}" \
            --dbpass "${MATRIX_REAL_DB_ROOT_PASSWORD}" \
            --pass "${MATRIX_WORDPRESS_ADMIN_PASSWORD}" \
            "Apps Matrix" \
            "${MATRIX_WORDPRESS_ADMIN_USER}"
        matrix_real_run_health_probes "${scenario}" "${source_dir}"
        matrix_real_rewrite_runtime_db_endpoint "${source_dir}/LocalSettings.php"
        cat >>"${source_dir}/LocalSettings.php" <<'PHP'
$wgScriptPath = "";
$wgArticlePath = "/wiki/$1";
$wgUsePathInfo = true;
PHP
        matrix_real_write_mediawiki_semantic_snapshot "${scenario}" "${source_dir}"
    fi
}

matrix_real_install_source() {
    local scenario="$1"
    local source_dir="$2"
    case "${scenario}" in
        wordpress-*) matrix_real_install_wordpress "${scenario}" "${source_dir}" ;;
        laravel) matrix_real_install_laravel "${scenario}" "${source_dir}" ;;
        drupal) matrix_real_install_drupal "${scenario}" "${source_dir}" ;;
        nextcloud) matrix_real_install_nextcloud "${scenario}" "${source_dir}" ;;
        joomla) matrix_real_install_joomla "${scenario}" "${source_dir}" ;;
        mediawiki) matrix_real_install_mediawiki "${scenario}" "${source_dir}" ;;
        *) matrix_fail "unknown real app scenario: ${scenario}"; return 1 ;;
    esac
}

matrix_real_source_docroot() {
    local scenario="$1"
    local source_dir="$2"
    case "${scenario}" in
        laravel) printf '%s/public\n' "${source_dir}" ;;
        drupal) printf '%s/web\n' "${source_dir}" ;;
        *) printf '%s\n' "${source_dir}" ;;
    esac
}

matrix_real_copy_to_engine_docroots() {
    local scenario="$1"
    local source_root="$2"
    local source_docroot="$3"
    local engine docroot volume_root
    matrix_real_prepare_runtime_permissions "${scenario}" "${source_docroot}"
    while IFS= read -r engine; do
        docroot="$(matrix_real_docroot "${engine}" "${scenario}")"
        volume_root="$(matrix_real_volume_root "${engine}" "${scenario}")"
        matrix_real_safe_clean_dir "${volume_root}"
        mkdir -p "${volume_root}"
        cp -a "${source_root}/." "${volume_root}/"
        mkdir -p "${docroot}"
        matrix_real_normalize_engine_htaccess "${engine}" "${docroot}"
        matrix_real_prepare_engine_runtime_ownership "${scenario}" "${engine}" "${docroot}" "${volume_root}"
        matrix_real_mark_managed "${volume_root}"
        matrix_real_mark_managed "${docroot}"
        matrix_real_copy_health_artifacts "${scenario}" "${docroot}"
    done < <(matrix_real_selected_engines)
}

matrix_real_copy_health_artifacts() {
    local scenario="$1"
    local docroot="$2"
    local source_dir="${MATRIX_OUT_DIR}/real-install-health/${scenario}"
    [[ -d "${source_dir}" ]] || return 0
    mkdir -p "${docroot}/.apps-matrix/health"
    cp -a "${source_dir}/." "${docroot}/.apps-matrix/health/"
}

matrix_real_normalize_engine_htaccess() {
    local engine="$1"
    local docroot="$2"
    local htaccess="${docroot}/.htaccess"
    [[ -f "${htaccess}" ]] || return 0

    case "${engine}" in
        ols_module|ols_native)
            python3 - "${htaccess}" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
filtered = [
    line
    for line in lines
    if not line.lstrip().startswith(("Options ", "DirectoryIndex "))
]
path.write_text("\n".join(filtered) + "\n", encoding="utf-8")
PY
            ;;
    esac
}

matrix_real_chmod_if_exists() {
    local mode="$1"
    shift
    local path
    for path in "$@"; do
        [[ -e "${path}" ]] || continue
        chmod -R "${mode}" "${path}" || true
    done
}

matrix_real_runtime_owner() {
    case "$1" in
        apache) printf '33:33\n' ;;
        ols_module|ols_native) printf '65534:65534\n' ;;
        *) return 1 ;;
    esac
}

matrix_real_chown_if_exists() {
    local owner="$1"
    shift
    local path
    for path in "$@"; do
        [[ -e "${path}" ]] || continue
        chown -R "${owner}" "${path}" || true
    done
}

matrix_real_prepare_engine_runtime_ownership() {
    local scenario="$1"
    local engine="$2"
    local docroot="$3"
    local volume_root="$4"
    local owner
    owner="$(matrix_real_runtime_owner "${engine}")" || return 0

    case "${scenario}" in
        wordpress-*)
            matrix_real_chown_if_exists "${owner}" "${docroot}/wp-content"
            ;;
        laravel)
            matrix_real_chown_if_exists "${owner}" \
                "${volume_root}/storage" \
                "${volume_root}/bootstrap/cache"
            ;;
        drupal)
            matrix_real_chown_if_exists "${owner}" "${docroot}/sites/default/files"
            ;;
        nextcloud)
            matrix_real_chown_if_exists "${owner}" \
                "${docroot}/config" \
                "${docroot}/data" \
                "${docroot}/apps" \
                "${docroot}/custom_apps" \
                "${docroot}/themes"
            ;;
        joomla)
            matrix_real_chown_if_exists "${owner}" \
                "${docroot}/administrator/cache" \
                "${docroot}/cache" \
                "${docroot}/logs" \
                "${docroot}/tmp"
            ;;
        mediawiki)
            matrix_real_chown_if_exists "${owner}" \
                "${docroot}/cache" \
                "${docroot}/images"
            ;;
    esac
}

matrix_real_prepare_runtime_permissions() {
    local scenario="$1"
    local source_docroot="$2"

    # Runtime containers use different PHP users (www-data, nobody). Keep the
    # fixture readable and only make application-owned runtime paths writable.
    matrix_real_chmod_if_exists a+rX "${source_docroot}"
    case "${scenario}" in
        wordpress-*)
            matrix_real_chmod_if_exists a+rwX "${source_docroot}/wp-content"
            ;;
        laravel)
            matrix_real_chmod_if_exists a+rwX \
                "${source_docroot}/storage" \
                "${source_docroot}/bootstrap/cache"
            ;;
        drupal)
            matrix_real_chmod_if_exists a+rwX \
                "${source_docroot}/sites/default/files"
            matrix_real_chmod_if_exists a+r \
                "${source_docroot}/sites/default/settings.php"
            ;;
        nextcloud)
            matrix_real_chmod_if_exists a+rwX \
                "${source_docroot}/config" \
                "${source_docroot}/data" \
                "${source_docroot}/apps" \
                "${source_docroot}/custom_apps" \
                "${source_docroot}/themes"
            ;;
        joomla)
            matrix_real_chmod_if_exists a+rwX \
                "${source_docroot}/administrator/cache" \
                "${source_docroot}/cache" \
                "${source_docroot}/logs" \
                "${source_docroot}/tmp"
            matrix_real_chmod_if_exists a+r \
                "${source_docroot}/configuration.php"
            ;;
        mediawiki)
            matrix_real_chmod_if_exists a+rwX \
                "${source_docroot}/cache" \
                "${source_docroot}/images"
            matrix_real_chmod_if_exists a+r \
                "${source_docroot}/LocalSettings.php"
            ;;
    esac
}

matrix_real_write_app_config_manifest() {
    local scenario="$1"
    local docroot="$2"
    local strict_plugin_install
    strict_plugin_install="$(matrix_real_effective_strict_plugin_install "${scenario}")"
    local args=(write --scenario "${scenario}" --docroot "${docroot}")
    if [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]]; then
        args+=(--app-install-enabled)
    fi
    if [[ "${MATRIX_REAL_DEEP_CONFIG}" == "1" ]]; then
        args+=(--deep-config-enabled)
    fi
    if [[ "${strict_plugin_install}" == "1" ]]; then
        args+=(--strict-plugin-install)
    fi
    if [[ "${MATRIX_REAL_HEALTH_PROBES}" == "1" ]]; then
        args+=(--health-probes-enabled)
    fi
    if [[ "${MATRIX_REAL_ENFORCE_HEALTH_PROBES}" == "1" ]]; then
        args+=(--enforce-health-probes)
    fi
    python3 "${MATRIX_DIR}/lib/real_app_config.py" "${args[@]}"
}

matrix_real_validate_app_config_manifest() {
    local scenario="$1"
    local docroot="$2"
    local strict_plugin_install
    strict_plugin_install="$(matrix_real_effective_strict_plugin_install "${scenario}")"
    local args=(validate --scenario "${scenario}" --docroot "${docroot}")
    if [[ "${MATRIX_REAL_RUN_APP_INSTALL}" == "1" ]]; then
        args+=(--app-install-enabled)
    fi
    if [[ "${MATRIX_REAL_DEEP_CONFIG}" == "1" ]]; then
        args+=(--deep-config-enabled)
    fi
    if [[ "${strict_plugin_install}" == "1" ]]; then
        args+=(--strict-plugin-install)
    fi
    if [[ "${MATRIX_REAL_HEALTH_PROBES}" == "1" ]]; then
        args+=(--health-probes-enabled)
    fi
    if [[ "${MATRIX_REAL_ENFORCE_HEALTH_PROBES}" == "1" ]]; then
        args+=(--enforce-health-probes)
    fi
    python3 "${MATRIX_DIR}/lib/real_app_config.py" "${args[@]}"
}

matrix_real_write_app_config_manifests() {
    local scenario="$1"
    local engine docroot
    while IFS= read -r engine; do
        docroot="$(matrix_real_docroot "${engine}" "${scenario}")"
        matrix_real_write_app_config_manifest "${scenario}" "${docroot}"
        if [[ "${MATRIX_REAL_VALIDATE_CONFIG}" == "1" ]]; then
            matrix_real_validate_app_config_manifest "${scenario}" "${docroot}"
        fi
    done < <(matrix_real_selected_engines)
}

matrix_real_seed_fallback_wordpress() {
    local scenario="$1"
    local docroot="$2"
    matrix_real_seed_wordpress_htaccess "${docroot}" "${scenario}"
}

matrix_real_append_once() {
    local file="$1"
    local marker="$2"
    if [[ -f "${file}" ]] && grep -Fq "${marker}" "${file}"; then
        return 0
    fi
    cat >>"${file}"
}

matrix_real_seed_fallback_laravel() {
    local docroot="$1"
    matrix_real_seed_common_php "${docroot}"
    [[ -f "${docroot}/.env" ]] || printf 'APP_ENV=testing\n' >"${docroot}/.env"
    matrix_real_append_once "${docroot}/.htaccess" '# apps-matrix fallback laravel' <<'HTACCESS'
# apps-matrix fallback laravel
<Files ".env">
  Require all denied
</Files>
HTACCESS
}

matrix_real_seed_fallback_drupal() {
    local docroot="$1"
    matrix_real_seed_common_php "${docroot}"
    mkdir -p "${docroot}/sites/default" "${docroot}/core/misc"
    [[ -f "${docroot}/sites/default/settings.php" ]] || printf "<?php\n// apps-matrix\n" >"${docroot}/sites/default/settings.php"
    [[ -f "${docroot}/core/misc/drupal.js" ]] || printf 'drupal js\n' >"${docroot}/core/misc/drupal.js"
    matrix_real_append_once "${docroot}/.htaccess" '# apps-matrix fallback drupal' <<'HTACCESS'
# apps-matrix fallback drupal
<Files "settings.php">
  Require all denied
</Files>
HTACCESS
}

matrix_real_seed_fallback_nextcloud() {
    local docroot="$1"
    matrix_real_seed_common_php "${docroot}"
    mkdir -p "${docroot}/config" "${docroot}/data"
    [[ -f "${docroot}/config/config.php" ]] || printf "<?php\n// apps-matrix\n" >"${docroot}/config/config.php"
    matrix_real_append_once "${docroot}/.htaccess" '# apps-matrix fallback nextcloud' <<'HTACCESS'
# apps-matrix fallback nextcloud
<Files "config.php">
  Require all denied
</Files>
RedirectMatch 403 ^/data/
HTACCESS
}

matrix_real_seed_fallback_joomla() {
    local docroot="$1"
    matrix_real_seed_common_php "${docroot}"
    mkdir -p "${docroot}/media/system/js" "${docroot}/administrator/cache"
    [[ -f "${docroot}/configuration.php" ]] || printf "<?php\n// apps-matrix\n" >"${docroot}/configuration.php"
    [[ -f "${docroot}/media/system/js/core.js" ]] || printf 'joomla js\n' >"${docroot}/media/system/js/core.js"
    cat >"${docroot}/administrator/cache/.htaccess" <<'HTACCESS'
RewriteEngine On
RewriteRule ^ - [F,L]
HTACCESS
    matrix_real_append_once "${docroot}/.htaccess" '# apps-matrix fallback joomla' <<'HTACCESS'
# apps-matrix fallback joomla
<Files "configuration.php">
  Require all denied
</Files>
HTACCESS
}

matrix_real_seed_fallback_mediawiki() {
    local docroot="$1"
    matrix_real_seed_common_php "${docroot}"
    mkdir -p "${docroot}/resources/assets" "${docroot}/maintenance"
    [[ -f "${docroot}/resources/assets/wiki.png" ]] || printf 'png fixture\n' >"${docroot}/resources/assets/wiki.png"
    [[ -f "${docroot}/maintenance/run.php" ]] || printf "<?php\n// apps-matrix\n" >"${docroot}/maintenance/run.php"
    matrix_real_append_once "${docroot}/.htaccess" '# apps-matrix fallback mediawiki' <<'HTACCESS'
# apps-matrix fallback mediawiki
<Files "run.php">
  Require all denied
</Files>
RewriteRule ^wiki/(.*)$ /index.php [L,QSA]
HTACCESS
}

matrix_real_seed_fallback_app_config_inputs() {
    local scenario="$1"
    local engine docroot
    while IFS= read -r engine; do
        docroot="$(matrix_real_docroot "${engine}" "${scenario}")"
        case "${scenario}" in
            wordpress-*) matrix_real_seed_fallback_wordpress "${scenario}" "${docroot}" ;;
            laravel) matrix_real_seed_fallback_laravel "${docroot}" ;;
            drupal) matrix_real_seed_fallback_drupal "${docroot}" ;;
            nextcloud) matrix_real_seed_fallback_nextcloud "${docroot}" ;;
            joomla) matrix_real_seed_fallback_joomla "${docroot}" ;;
            mediawiki) matrix_real_seed_fallback_mediawiki "${docroot}" ;;
        esac
    done < <(matrix_real_selected_engines)
}

matrix_real_install_fixture() {
    local scenario="$1"
    local cases_yaml="$2"
    local app="${scenario}"
    local source_dir source_docroot

    matrix_real_init_observability
    matrix_real_write_plan "${scenario}" "${app}"
    if [[ "${MATRIX_REAL_ALLOW_NETWORK}" != "1" ]]; then
        matrix_warn "${scenario}: real installer plan written; set MATRIX_REAL_ALLOW_NETWORK=1 to download app sources"
        MATRIX_WRITE_HTACCESS=1 python3 "${MATRIX_DIR}/lib/provision_fixture.py" \
            --scenario "${scenario}" \
            --cases "${cases_yaml}" \
            --probes-dir "${MATRIX_DIR}/probes" \
            --mode real \
            --out-dir "${MATRIX_OUT_DIR}"
        matrix_real_seed_fallback_app_config_inputs "${scenario}"
        matrix_real_write_app_config_manifests "${scenario}"
        return 0
    fi

    source_dir="$(matrix_real_prepare_source "${scenario}")"
    matrix_real_install_source "${scenario}" "${source_dir}"
    source_docroot="$(matrix_real_source_docroot "${scenario}" "${source_dir}")"
    matrix_real_seed_common_php "${source_docroot}"
    matrix_real_copy_to_engine_docroots "${scenario}" "${source_dir}" "${source_docroot}"
    matrix_real_write_app_config_manifests "${scenario}"

    python3 "${MATRIX_DIR}/lib/provision_fixture.py" \
        --scenario "${scenario}" \
        --cases "${cases_yaml}" \
        --probes-dir "${MATRIX_DIR}/probes" \
        --mode real \
        --out-dir "${MATRIX_OUT_DIR}"
}
