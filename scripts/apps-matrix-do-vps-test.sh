#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_ROOT="${APPS_MATRIX_DO_OUT:-${ROOT_DIR}/tests/e2e/apps-matrix/out/do-vps}"
WORK_DIR="/root/ols-extra"
NAME="${APPS_MATRIX_DO_NAME:-apps-matrix-$(date +%Y%m%d%H%M%S)}"
REGION="${APPS_MATRIX_DO_REGION:-sfo3}"
SIZE="${APPS_MATRIX_DO_SIZE:-s-4vcpu-8gb}"
IMAGE="${APPS_MATRIX_DO_IMAGE:-ubuntu-24-04-x64}"
HEAVY_SCENARIOS="wordpress-core,wordpress-redirection,wordpress-w3-total-cache,wordpress-litespeed-cache,wordpress-wordfence,wordpress-ewww,wordpress-wp-optimize,laravel,drupal,nextcloud,joomla,mediawiki"
WORDPRESS_SCENARIOS="wordpress-core,wordpress-redirection,wordpress-w3-total-cache,wordpress-litespeed-cache,wordpress-wordfence,wordpress-ewww,wordpress-wp-optimize"
CMS_SCENARIOS="drupal,nextcloud,joomla,mediawiki"
FRAMEWORK_SCENARIOS="laravel"
SCENARIOS="${APPS_MATRIX_DO_SCENARIOS:-wordpress-core}"
ENGINES="${APPS_MATRIX_DO_ENGINES:-apache,ols_module}"
PHASE="${APPS_MATRIX_DO_PHASE:-all}"
KEEP_DROPLET="${APPS_MATRIX_DO_KEEP_DROPLET:-0}"
STRICT_PLUGIN_INSTALL="${APPS_MATRIX_DO_STRICT_PLUGIN_INSTALL:-auto}"
HEALTH_PROBES="${APPS_MATRIX_DO_HEALTH_PROBES:-1}"
ENFORCE_HEALTH_PROBES="${APPS_MATRIX_DO_ENFORCE_HEALTH_PROBES:-1}"
ENFORCE_BUDGETS="${APPS_MATRIX_DO_ENFORCE_BUDGETS:-0}"
CONTINUE_ON_FAILURE="${APPS_MATRIX_DO_CONTINUE_ON_FAILURE:-0}"
ARTIFACT_MODE="${APPS_MATRIX_DO_ARTIFACT_MODE:-summary}"
RSYNC_RETRIES="${APPS_MATRIX_DO_RSYNC_RETRIES:-5}"
SSH_RETRIES="${APPS_MATRIX_DO_SSH_RETRIES:-5}"
COMPOSE_BUILD="${APPS_MATRIX_DO_COMPOSE_BUILD:-1}"
APACHE_IMAGE="${APPS_MATRIX_DO_APACHE_IMAGE:-}"
OLS_MODULE_IMAGE="${APPS_MATRIX_DO_OLS_MODULE_IMAGE:-}"
OLS_BUILD_JOBS="${APPS_MATRIX_DO_OLS_BUILD_JOBS:-}"
OLS_RPM_BUILD_MODE="${APPS_MATRIX_DO_OLS_RPM_BUILD_MODE:-}"
SKIP_DEP_INSTALL="${APPS_MATRIX_DO_SKIP_DEP_INSTALL:-0}"
PREPULL_IMAGES="${APPS_MATRIX_DO_PREPULL_IMAGES:-1}"
DOCKER_REGISTRY="${APPS_MATRIX_DO_DOCKER_REGISTRY:-}"
DOCKER_USERNAME="${APPS_MATRIX_DO_DOCKER_USERNAME:-}"
DOCKER_PASSWORD="${APPS_MATRIX_DO_DOCKER_PASSWORD:-}"
RUN_TIMEOUT_SECONDS="${APPS_MATRIX_DO_RUN_TIMEOUT_SECONDS:-7200}"
SSH_KEY_PATH="${APPS_MATRIX_DO_SSH_KEY:-${OUT_ROOT}/${NAME}_id_ed25519}"
GENERATED_SSH_KEY=0
DROPLET_CREATE_ATTEMPTED=0
REMOTE_OUT="/root/apps-matrix-out"
LOCAL_REMOTE_ENV=""
LOCAL_REMOTE_SCRIPT=""
LOCAL_REMOTE_LAUNCHER=""

usage() {
    cat <<EOF
Usage: DIGITALOCEAN_ACCESS_TOKEN=... $0

Environment:
  APPS_MATRIX_DO_SCENARIOS   Scenario list, all, heavy, wordpress, cms, or framework. Default: wordpress-core
  APPS_MATRIX_DO_ENGINES     Engine list. Default: apache,ols_module
                              all phase requires apache and ols_module.
  APPS_MATRIX_DO_REGION      DigitalOcean region. Default: sfo3
  APPS_MATRIX_DO_SIZE        Droplet size. Default: s-4vcpu-8gb
  APPS_MATRIX_DO_IMAGE       Droplet image. Default: ubuntu-24-04-x64
  APPS_MATRIX_DO_PHASE       all or install. Default: all
  APPS_MATRIX_DO_STRICT_PLUGIN_INSTALL auto, 0, or 1. Default: auto
  APPS_MATRIX_DO_HEALTH_PROBES 0 or 1. Default: 1
  APPS_MATRIX_DO_ENFORCE_HEALTH_PROBES 0 or 1. Default: 1
  APPS_MATRIX_DO_ENFORCE_BUDGETS 0 or 1. Default: 0
  APPS_MATRIX_DO_CONTINUE_ON_FAILURE 0 or 1. Default: 0
  APPS_MATRIX_DO_ARTIFACT_MODE summary or full. Default: summary
  APPS_MATRIX_DO_RSYNC_RETRIES positive integer. Default: 5
  APPS_MATRIX_DO_SSH_RETRIES positive integer. Default: 5
  APPS_MATRIX_DO_COMPOSE_BUILD 0 or 1. Default: 1
  APPS_MATRIX_DO_APACHE_IMAGE Optional prebuilt Apache runtime image.
  APPS_MATRIX_DO_OLS_MODULE_IMAGE Optional prebuilt ols_module image.
  APPS_MATRIX_DO_OLS_BUILD_JOBS Optional patched OLS build parallelism.
  APPS_MATRIX_DO_OLS_RPM_BUILD_MODE Optional build or package. Dockerfile default: build
  APPS_MATRIX_DO_SKIP_DEP_INSTALL 0 or 1. Set to 1 for prepared DO snapshots. Default: 0
  APPS_MATRIX_DO_PREPULL_IMAGES 0 or 1. Pre-pull prebuilt runtime images. Default: 1
  APPS_MATRIX_DO_DOCKER_REGISTRY Optional Docker registry for private image login.
  APPS_MATRIX_DO_DOCKER_USERNAME Optional Docker registry username.
  APPS_MATRIX_DO_DOCKER_PASSWORD Optional Docker registry password or token.
  APPS_MATRIX_DO_RUN_TIMEOUT_SECONDS Remote matrix timeout. Default: 7200
  APPS_MATRIX_DO_KEEP_DROPLET=1 keeps the droplet for manual debugging.
EOF
}

log() {
    printf '==> %s\n' "$*"
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

shell_quote() {
    printf '%q' "$1"
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "missing required tool: $1"
}

api() {
    local method="$1"
    local path="$2"
    local data="${3:-}"
    local args=(-fsS -X "${method}" "https://api.digitalocean.com/v2${path}" -H "Content-Type: application/json")
    local token="${DIGITALOCEAN_ACCESS_TOKEN//\\/\\\\}"
    token="${token//\"/\\\"}"
    if [[ -n "${data}" ]]; then
        args+=(-d "${data}")
    fi
    printf 'header = "Authorization: Bearer %s"\n' "${token}" | curl --config - "${args[@]}"
}

json_get() {
    local input
    input="$(cat)"
    JSON_INPUT="${input}" python3 - "$1" <<'PY'
import json
import os
import sys

path = sys.argv[1].split(".")
data = json.loads(os.environ["JSON_INPUT"])
for part in path:
    if isinstance(data, list):
        data = data[int(part)]
    else:
        data = data[part]
print(data)
PY
}

json_get_or_empty() {
    local input
    input="$(cat)"
    JSON_INPUT="${input}" python3 - "$1" <<'PY'
import json
import os
import sys

path = sys.argv[1].split(".")
try:
    data = json.loads(os.environ["JSON_INPUT"])
    for part in path:
        if isinstance(data, list):
            data = data[int(part)]
        else:
            data = data[part]
except (KeyError, IndexError, ValueError, json.JSONDecodeError):
    sys.exit(0)
print(data)
PY
}

json_has_key() {
    local input
    input="$(cat)"
    JSON_INPUT="${input}" python3 - "$1" <<'PY'
import json
import os
import sys

path = sys.argv[1].split(".")
try:
    data = json.loads(os.environ["JSON_INPUT"])
    for part in path:
        if isinstance(data, list):
            data = data[int(part)]
        else:
            data = data[part]
except (KeyError, IndexError, ValueError, json.JSONDecodeError):
    sys.exit(1)
sys.exit(0)
PY
}

api_error_message() {
    local input
    input="$(cat)"
    JSON_INPUT="${input}" python3 - <<'PY'
import json
import os

try:
    data = json.loads(os.environ["JSON_INPUT"])
except json.JSONDecodeError:
    print(os.environ["JSON_INPUT"])
else:
    print(data.get("message") or data.get("id") or data)
PY
}

api_checked() {
    local method="$1"
    local path="$2"
    local data="${3:-}"
    local response
    local attempt max_attempts
    max_attempts="${APPS_MATRIX_DO_API_RETRIES:-5}"
    for attempt in $(seq 1 "${max_attempts}"); do
        if response="$(api "${method}" "${path}" "${data}")"; then
            break
        fi
        if [[ "${attempt}" == "${max_attempts}" ]]; then
            fail "DigitalOcean API request failed: ${method} ${path}"
        fi
        log "DigitalOcean API request retry ${attempt}/${max_attempts}: ${method} ${path}"
        sleep $((attempt * 3))
    done
    if printf '%s' "${response}" | json_has_key message; then
        local message
        message="$(printf '%s' "${response}" | api_error_message)"
        fail "DigitalOcean API error: ${message}"
    fi
    printf '%s' "${response}"
}

api_once_checked() {
    local method="$1"
    local path="$2"
    local data="${3:-}"
    local response
    if ! response="$(api "${method}" "${path}" "${data}")"; then
        return 1
    fi
    if printf '%s' "${response}" | json_has_key message; then
        local message
        message="$(printf '%s' "${response}" | api_error_message)"
        fail "DigitalOcean API error: ${message}"
    fi
    printf '%s' "${response}"
}

droplet_ids_by_name() {
    local response
    response="$(api_checked GET "/droplets?tag_name=apps-matrix&per_page=200")"
    DROPLET_NAME="${NAME}" JSON_INPUT="${response}" python3 - <<'PY'
import json
import os

target = os.environ["DROPLET_NAME"]
data = json.loads(os.environ["JSON_INPUT"])
for droplet in data.get("droplets", []):
    if droplet.get("name") == target and "temporary" in droplet.get("tags", []):
        print(droplet["id"])
PY
}

cleanup_droplet() {
    if [[ "${KEEP_DROPLET}" != "1" ]]; then
        local droplet_ids=()
        local id known_id
        if [[ -n "${DROPLET_ID:-}" ]]; then
            droplet_ids+=("${DROPLET_ID}")
        fi
        if [[ "${DROPLET_CREATE_ATTEMPTED}" == "1" ]]; then
            while IFS= read -r id; do
                [[ -n "${id}" ]] || continue
                for known_id in "${droplet_ids[@]}"; do
                    [[ "${known_id}" == "${id}" ]] && continue 2
                done
                droplet_ids+=("${id}")
            done < <(droplet_ids_by_name 2>/dev/null || true)
        fi
        for id in "${droplet_ids[@]}"; do
            log "destroying droplet ${id}"
            api DELETE "/droplets/${id}" >/dev/null || true
        done
    elif [[ -n "${DROPLET_ID:-}" ]]; then
        log "keeping droplet ${DROPLET_ID}"
    fi
    if [[ -n "${SSH_KEY_ID:-}" ]]; then
        log "removing temporary DigitalOcean SSH key ${SSH_KEY_ID}"
        api DELETE "/account/keys/${SSH_KEY_ID}" >/dev/null || true
    fi
    if [[ "${GENERATED_SSH_KEY}" == "1" && -n "${SSH_KEY_PATH:-}" ]]; then
        rm -f "${SSH_KEY_PATH}" "${SSH_KEY_PATH}.pub"
    fi
    if [[ -n "${LOCAL_REMOTE_ENV:-}" ]]; then
        rm -f "${LOCAL_REMOTE_ENV}"
    fi
    if [[ -n "${LOCAL_REMOTE_SCRIPT:-}" ]]; then
        rm -f "${LOCAL_REMOTE_SCRIPT}"
    fi
    if [[ -n "${LOCAL_REMOTE_LAUNCHER:-}" ]]; then
        rm -f "${LOCAL_REMOTE_LAUNCHER}"
    fi
}
trap cleanup_droplet EXIT

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

[[ -n "${DIGITALOCEAN_ACCESS_TOKEN:-}" ]] || fail "DIGITALOCEAN_ACCESS_TOKEN is required"
case "${PHASE}" in
    all|install) ;;
    *) fail "APPS_MATRIX_DO_PHASE must be all or install" ;;
esac
case "${SCENARIOS}" in
    heavy) SCENARIOS="${HEAVY_SCENARIOS}" ;;
    wordpress) SCENARIOS="${WORDPRESS_SCENARIOS}" ;;
    cms) SCENARIOS="${CMS_SCENARIOS}" ;;
    framework) SCENARIOS="${FRAMEWORK_SCENARIOS}" ;;
esac
case "${SCENARIOS}" in
    all) ;;
    *)
        IFS=, read -r -a scenario_values <<<"${SCENARIOS}"
        for scenario_value in "${scenario_values[@]}"; do
            case "${scenario_value}" in
                wordpress-core|wordpress-redirection|wordpress-w3-total-cache|wordpress-litespeed-cache|wordpress-wordfence|wordpress-ewww|wordpress-wp-optimize|laravel|drupal|nextcloud|joomla|mediawiki) ;;
                *) fail "unsupported APPS_MATRIX_DO_SCENARIOS value: ${scenario_value}" ;;
            esac
        done
        ;;
esac
IFS=, read -r -a engine_values <<<"${ENGINES}"
for engine_value in "${engine_values[@]}"; do
    case "${engine_value}" in
        apache|ols_module|ols_native) ;;
        *) fail "unsupported APPS_MATRIX_DO_ENGINES value: ${engine_value}" ;;
    esac
done
engines_normalized=",${ENGINES//[[:space:]]/},"
if [[ "${PHASE}" != "install" ]]; then
    if [[ "${engines_normalized}" != *",apache,"* || "${engines_normalized}" != *",ols_module,"* ]]; then
        fail "APPS_MATRIX_DO_ENGINES must include apache and ols_module when APPS_MATRIX_DO_PHASE=${PHASE}"
    fi
fi
case "${STRICT_PLUGIN_INSTALL}" in
    auto|0|1) ;;
    *) fail "APPS_MATRIX_DO_STRICT_PLUGIN_INSTALL must be auto, 0, or 1" ;;
esac
case "${HEALTH_PROBES}" in
    0|1) ;;
    *) fail "APPS_MATRIX_DO_HEALTH_PROBES must be 0 or 1" ;;
esac
case "${ENFORCE_HEALTH_PROBES}" in
    0|1) ;;
    *) fail "APPS_MATRIX_DO_ENFORCE_HEALTH_PROBES must be 0 or 1" ;;
esac
case "${ENFORCE_BUDGETS}" in
    0|1) ;;
    *) fail "APPS_MATRIX_DO_ENFORCE_BUDGETS must be 0 or 1" ;;
esac
case "${CONTINUE_ON_FAILURE}" in
    0|1) ;;
    *) fail "APPS_MATRIX_DO_CONTINUE_ON_FAILURE must be 0 or 1" ;;
esac
case "${ARTIFACT_MODE}" in
    summary|full) ;;
    *) fail "APPS_MATRIX_DO_ARTIFACT_MODE must be summary or full" ;;
esac
case "${COMPOSE_BUILD}" in
    0|1) ;;
    *) fail "APPS_MATRIX_DO_COMPOSE_BUILD must be 0 or 1" ;;
esac
case "${SKIP_DEP_INSTALL}" in
    0|1) ;;
    *) fail "APPS_MATRIX_DO_SKIP_DEP_INSTALL must be 0 or 1" ;;
esac
case "${PREPULL_IMAGES}" in
    0|1) ;;
    *) fail "APPS_MATRIX_DO_PREPULL_IMAGES must be 0 or 1" ;;
esac
if [[ -n "${APACHE_IMAGE}" && ! "${APACHE_IMAGE}" =~ ^[A-Za-z0-9._:@/-]+$ ]]; then
    fail "APPS_MATRIX_DO_APACHE_IMAGE contains unsupported characters"
fi
if [[ ! "${RSYNC_RETRIES}" =~ ^[1-9][0-9]*$ ]]; then
    fail "APPS_MATRIX_DO_RSYNC_RETRIES must be a positive integer"
fi
if [[ ! "${SSH_RETRIES}" =~ ^[1-9][0-9]*$ ]]; then
    fail "APPS_MATRIX_DO_SSH_RETRIES must be a positive integer"
fi
if [[ -n "${OLS_MODULE_IMAGE}" && ! "${OLS_MODULE_IMAGE}" =~ ^[A-Za-z0-9._:@/-]+$ ]]; then
    fail "APPS_MATRIX_DO_OLS_MODULE_IMAGE contains unsupported characters"
fi
if [[ -n "${OLS_BUILD_JOBS}" && ! "${OLS_BUILD_JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    fail "APPS_MATRIX_DO_OLS_BUILD_JOBS must be a positive integer when set"
fi
case "${OLS_RPM_BUILD_MODE}" in
    ""|build|package) ;;
    *) fail "APPS_MATRIX_DO_OLS_RPM_BUILD_MODE must be build or package when set" ;;
esac
if [[ -n "${DOCKER_REGISTRY}" && ! "${DOCKER_REGISTRY}" =~ ^[A-Za-z0-9._:/-]+$ ]]; then
    fail "APPS_MATRIX_DO_DOCKER_REGISTRY contains unsupported characters"
fi
if [[ -n "${DOCKER_USERNAME}" && ! "${DOCKER_USERNAME}" =~ ^[A-Za-z0-9._@:+-]+$ ]]; then
    fail "APPS_MATRIX_DO_DOCKER_USERNAME contains unsupported characters"
fi
if [[ -n "${DOCKER_USERNAME}" || -n "${DOCKER_PASSWORD}" ]]; then
    [[ -n "${DOCKER_USERNAME}" && -n "${DOCKER_PASSWORD}" ]] || \
        fail "APPS_MATRIX_DO_DOCKER_USERNAME and APPS_MATRIX_DO_DOCKER_PASSWORD must be provided together"
fi
if [[ "${COMPOSE_BUILD}" == "0" ]]; then
    [[ -n "${APACHE_IMAGE}" ]] || fail "APPS_MATRIX_DO_APACHE_IMAGE is required when APPS_MATRIX_DO_COMPOSE_BUILD=0"
    [[ -n "${OLS_MODULE_IMAGE}" ]] || fail "APPS_MATRIX_DO_OLS_MODULE_IMAGE is required when APPS_MATRIX_DO_COMPOSE_BUILD=0"
fi
if [[ ! "${RUN_TIMEOUT_SECONDS}" =~ ^[1-9][0-9]*$ ]]; then
    fail "APPS_MATRIX_DO_RUN_TIMEOUT_SECONDS must be a positive integer"
fi
require_tool curl
require_tool python3
require_tool rsync
require_tool ssh
require_tool ssh-keygen
require_tool tar

mkdir -p "${OUT_ROOT}"
chmod 700 "${OUT_ROOT}"
rm -rf "${OUT_ROOT}/apps-matrix-out" "${OUT_ROOT}/apps-matrix-out.tgz"

log "expanded scenarios: ${SCENARIOS}"
log "engines: ${ENGINES}; phase: ${PHASE}; continue_on_failure: ${CONTINUE_ON_FAILURE}"
log "compose_build: ${COMPOSE_BUILD}; apache_image: ${APACHE_IMAGE:-<default>}; ols_module_image: ${OLS_MODULE_IMAGE:-<default>}"
log "ols_build_jobs: ${OLS_BUILD_JOBS:-<default>}; ols_rpm_build_mode: ${OLS_RPM_BUILD_MODE:-<default>}"
log "skip_dep_install: ${SKIP_DEP_INSTALL}; prepull_images: ${PREPULL_IMAGES}; droplet_image: ${IMAGE}"

if [[ ! -f "${SSH_KEY_PATH}" ]]; then
    log "creating temporary SSH key ${SSH_KEY_PATH}"
    ssh-keygen -t ed25519 -N "" -f "${SSH_KEY_PATH}" -C "apps-matrix-do-vps" >/dev/null
    GENERATED_SSH_KEY=1
fi
PUB_KEY="$(<"${SSH_KEY_PATH}.pub")"

log "registering SSH key"
key_response="$(api_checked POST /account/keys "$(python3 - "${PUB_KEY}" <<'PY'
import json
import sys

print(json.dumps({"name": "apps-matrix-do-vps", "public_key": sys.argv[1]}))
PY
)")"
SSH_KEY_ID="$(printf '%s' "${key_response}" | json_get ssh_key.id)"

log "creating droplet ${NAME}"
create_payload="$(python3 - "${NAME}" "${REGION}" "${SIZE}" "${IMAGE}" "${SSH_KEY_ID}" <<'PY'
import json
import sys

name, region, size, image, key_id = sys.argv[1:]
print(json.dumps({
    "name": name,
    "region": region,
    "size": size,
    "image": image,
    "ssh_keys": [int(key_id)],
    "ipv6": False,
    "monitoring": True,
    "tags": ["apps-matrix", "temporary"],
}))
PY
)"
DROPLET_CREATE_ATTEMPTED=1
create_response=""
set +e
create_response="$(api_once_checked POST /droplets "${create_payload}")"
create_status=$?
set -e
if [[ "${create_status}" -eq 0 ]]; then
    DROPLET_ID="$(printf '%s' "${create_response}" | json_get droplet.id)"
else
    log "droplet create response was not received; checking for droplet by name"
    for _ in $(seq 1 12); do
        mapfile -t matched_droplets < <(droplet_ids_by_name)
        case "${#matched_droplets[@]}" in
            0)
                sleep 5
                ;;
            1)
                DROPLET_ID="${matched_droplets[0]}"
                log "found created droplet ${DROPLET_ID} after lost create response"
                break
                ;;
            *)
                fail "multiple droplets named ${NAME} exist after create response failure; refusing to continue"
                ;;
        esac
    done
    [[ -n "${DROPLET_ID:-}" ]] || fail "droplet create failed and no droplet named ${NAME} was found"
fi

log "waiting for droplet IP"
ip=""
for _ in $(seq 1 90); do
    detail="$(api_checked GET "/droplets/${DROPLET_ID}")"
    ip="$(DETAIL="${detail}" python3 - <<'PY'
import json
import os
import sys

data = json.loads(os.environ["DETAIL"])
for net in data["droplet"]["networks"]["v4"]:
    if net.get("type") == "public":
        print(net["ip_address"])
        break
PY
)"
    [[ -n "${ip}" ]] && break
    sleep 5
done
[[ -n "${ip}" ]] || fail "droplet did not receive a public IPv4"

ssh_opts=(-i "${SSH_KEY_PATH}" -o StrictHostKeyChecking=no -o UserKnownHostsFile="${OUT_ROOT}/known_hosts" -o ConnectTimeout=10)
rsync_ssh="ssh -i ${SSH_KEY_PATH} -o StrictHostKeyChecking=no -o UserKnownHostsFile=${OUT_ROOT}/known_hosts -o ConnectTimeout=10"
ssh_retry() {
    local label="$1"
    shift
    local attempt
    for attempt in $(seq 1 "${SSH_RETRIES}"); do
        # shellcheck disable=SC2029  # Call sites intentionally pass client-quoted remote commands.
        if ssh "${ssh_opts[@]}" "$@"; then
            return 0
        fi
        if [[ "${attempt}" == "${SSH_RETRIES}" ]]; then
            return 1
        fi
        log "${label} SSH retry ${attempt}/${SSH_RETRIES}"
        sleep $((attempt * 5))
    done
}
scp_retry() {
    local label="$1"
    shift
    local attempt
    for attempt in $(seq 1 "${SSH_RETRIES}"); do
        if scp "${ssh_opts[@]}" "$@"; then
            return 0
        fi
        if [[ "${attempt}" == "${SSH_RETRIES}" ]]; then
            return 1
        fi
        log "${label} SCP retry ${attempt}/${SSH_RETRIES}"
        sleep $((attempt * 5))
    done
}
log "waiting for SSH at ${ip}"
for _ in $(seq 1 90); do
    if ssh "${ssh_opts[@]}" "root@${ip}" true >/dev/null 2>&1; then
        break
    fi
    sleep 5
done
ssh "${ssh_opts[@]}" "root@${ip}" true >/dev/null 2>&1 || fail "SSH did not become ready"

if [[ "${SKIP_DEP_INSTALL}" == "1" ]]; then
    log "checking remote dependencies from prepared image"
ssh "${ssh_opts[@]}" "root@${ip}" 'set -eux
for cmd in bash bzip2 composer curl docker git gzip php rsync tar unzip; do
  command -v "$cmd"
done
command -v mariadb || command -v mysql
docker version
docker compose version
php -v
composer --version
'
else
    log "installing remote dependencies"
    ssh "${ssh_opts[@]}" "root@${ip}" 'set -eux
export DEBIAN_FRONTEND=noninteractive
wait_for_apt() {
  if ! command -v fuser >/dev/null 2>&1; then
    return 0
  fi
  for _ in $(seq 1 120); do
    if ! fuser /var/lib/dpkg/lock-frontend /var/lib/dpkg/lock /var/lib/apt/lists/lock /var/cache/apt/archives/lock >/dev/null 2>&1; then
      return 0
    fi
    sleep 5
  done
  return 1
}
if command -v cloud-init >/dev/null 2>&1; then
  cloud-init status --wait || true
fi
wait_for_apt
apt-get update
wait_for_apt
apt-get install -y ca-certificates curl git rsync tar gzip bzip2 unzip composer default-mysql-client mariadb-client php-cli php-curl php-gd php-intl php-mbstring php-mysql php-xml php-zip
install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
chmod a+r /etc/apt/keyrings/docker.asc
. /etc/os-release
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu ${VERSION_CODENAME} stable" > /etc/apt/sources.list.d/docker.list
wait_for_apt
apt-get update
wait_for_apt
apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
docker version
docker compose version
'
fi

if [[ -n "${DOCKER_USERNAME}" ]]; then
    registry_label="${DOCKER_REGISTRY:-Docker Hub}"
    log "logging in to Docker registry ${registry_label}"
    registry_arg=""
    if [[ -n "${DOCKER_REGISTRY}" ]]; then
        registry_arg="$(shell_quote "${DOCKER_REGISTRY}")"
    fi
    # shellcheck disable=SC2029  # Values are intentionally client-quoted before remote execution.
    printf '%s' "${DOCKER_PASSWORD}" | ssh "${ssh_opts[@]}" "root@${ip}" \
        "docker login ${registry_arg} --username $(shell_quote "${DOCKER_USERNAME}") --password-stdin" >/dev/null
fi

if [[ "${COMPOSE_BUILD}" == "0" && "${PREPULL_IMAGES}" == "1" && -n "${APACHE_IMAGE}" ]]; then
    log "pre-pulling Apache runtime image ${APACHE_IMAGE}"
    # shellcheck disable=SC2029  # Image reference is intentionally client-quoted before remote execution.
    ssh "${ssh_opts[@]}" "root@${ip}" \
        "docker pull $(shell_quote "${APACHE_IMAGE}")" >/dev/null
fi

if [[ "${COMPOSE_BUILD}" == "0" && "${PREPULL_IMAGES}" == "1" && -n "${OLS_MODULE_IMAGE}" ]]; then
    log "pre-pulling OLS module image ${OLS_MODULE_IMAGE}"
    # shellcheck disable=SC2029  # Image reference is intentionally client-quoted before remote execution.
    ssh "${ssh_opts[@]}" "root@${ip}" \
        "docker pull $(shell_quote "${OLS_MODULE_IMAGE}")" >/dev/null
fi

log "syncing workspace to droplet"
sync_workspace() {
    rsync -az --delete \
        --exclude .git \
        --exclude build \
        --exclude build-sanitizer \
        --exclude build-release-arm64 \
        --exclude docs/node_modules \
        --exclude tests/e2e/apps-matrix/out \
        --exclude public-repo/litehttpd/.git \
        -e "${rsync_ssh}" \
        "${ROOT_DIR}/" "root@${ip}:${WORK_DIR}/"
}
sync_ok=0
for attempt in $(seq 1 "${RSYNC_RETRIES}"); do
    if sync_workspace; then
        sync_ok=1
        break
    fi
    log "workspace sync retry ${attempt}/${RSYNC_RETRIES}"
    sleep $((attempt * 5))
    ssh "${ssh_opts[@]}" "root@${ip}" true >/dev/null 2>&1 || true
done
[[ "${sync_ok}" == "1" ]] || fail "workspace sync failed after ${RSYNC_RETRIES} attempts"

log "running apps-matrix heavy reality test"
LOCAL_REMOTE_ENV="$(mktemp)"
LOCAL_REMOTE_SCRIPT="$(mktemp)"
LOCAL_REMOTE_LAUNCHER="$(mktemp)"
{
    printf 'WORK_DIR=%q\n' "${WORK_DIR}"
    printf 'REMOTE_OUT=%q\n' "${REMOTE_OUT}"
    printf 'SCENARIOS=%q\n' "${SCENARIOS}"
    printf 'ENGINES=%q\n' "${ENGINES}"
    printf 'PHASE=%q\n' "${PHASE}"
    printf 'CONTINUE_ON_FAILURE=%q\n' "${CONTINUE_ON_FAILURE}"
    printf 'COMPOSE_BUILD=%q\n' "${COMPOSE_BUILD}"
    printf 'APACHE_IMAGE=%q\n' "${APACHE_IMAGE}"
    printf 'OLS_MODULE_IMAGE=%q\n' "${OLS_MODULE_IMAGE}"
    printf 'OLS_BUILD_JOBS=%q\n' "${OLS_BUILD_JOBS}"
    printf 'OLS_RPM_BUILD_MODE=%q\n' "${OLS_RPM_BUILD_MODE}"
    printf 'SKIP_DEP_INSTALL=%q\n' "${SKIP_DEP_INSTALL}"
    printf 'PREPULL_IMAGES=%q\n' "${PREPULL_IMAGES}"
    printf 'STRICT_PLUGIN_INSTALL=%q\n' "${STRICT_PLUGIN_INSTALL}"
    printf 'HEALTH_PROBES=%q\n' "${HEALTH_PROBES}"
    printf 'ENFORCE_HEALTH_PROBES=%q\n' "${ENFORCE_HEALTH_PROBES}"
    printf 'ENFORCE_BUDGETS=%q\n' "${ENFORCE_BUDGETS}"
    printf 'ARTIFACT_MODE=%q\n' "${ARTIFACT_MODE}"
} > "${LOCAL_REMOTE_ENV}"
cat > "${LOCAL_REMOTE_SCRIPT}" <<'REMOTE_SCRIPT'
#!/usr/bin/env bash
set -ux
# shellcheck disable=SC1091
source /root/apps-matrix-run.env

cd "${WORK_DIR}"
mkdir -p "${REMOTE_OUT}"
cat > "${REMOTE_OUT}/scenario-selection.txt" <<EOF
scenarios=${SCENARIOS}
engines=${ENGINES}
phase=${PHASE}
continue_on_failure=${CONTINUE_ON_FAILURE}
compose_build=${COMPOSE_BUILD}
apache_image=${APACHE_IMAGE}
ols_module_image=${OLS_MODULE_IMAGE}
ols_build_jobs=${OLS_BUILD_JOBS}
ols_rpm_build_mode=${OLS_RPM_BUILD_MODE}
skip_dep_install=${SKIP_DEP_INSTALL}
prepull_images=${PREPULL_IMAGES}
EOF

set +e
MATRIX_OUT_DIR="${REMOTE_OUT}" \
MATRIX_SCENARIOS="${SCENARIOS}" \
MATRIX_FULL_PHASE="${PHASE}" \
MATRIX_CONTINUE_ON_FAILURE="${CONTINUE_ON_FAILURE}" \
MATRIX_FIXTURE_MODE=real \
MATRIX_REAL_ALLOW_NETWORK=1 \
MATRIX_REAL_RUN_APP_INSTALL=1 \
MATRIX_REAL_DEEP_CONFIG=1 \
MATRIX_REAL_VALIDATE_CONFIG=1 \
MATRIX_REAL_STRICT_PLUGIN_INSTALL="${STRICT_PLUGIN_INSTALL}" \
MATRIX_REAL_HEALTH_PROBES="${HEALTH_PROBES}" \
MATRIX_REAL_ENFORCE_HEALTH_PROBES="${ENFORCE_HEALTH_PROBES}" \
MATRIX_ENGINES="${ENGINES}" \
MATRIX_COMPOSE_BUILD="${COMPOSE_BUILD}" \
MATRIX_APACHE_IMAGE="${APACHE_IMAGE}" \
MATRIX_OLS_MODULE_IMAGE="${OLS_MODULE_IMAGE}" \
MATRIX_OLS_BUILD_JOBS="${OLS_BUILD_JOBS}" \
MATRIX_OLS_RPM_BUILD_MODE="${OLS_RPM_BUILD_MODE}" \
MATRIX_PER_ENGINE_BUDGET_MS=30000 \
MATRIX_ENFORCE_PERF_BUDGET="${ENFORCE_BUDGETS}" \
MATRIX_ENFORCE_SCENARIO_BUDGET="${ENFORCE_BUDGETS}" \
bash tests/e2e/apps-matrix/run_full_matrix.sh
matrix_status=$?
set -e

python3 tests/e2e/apps-matrix/lib/tune_budgets.py --out-dir "${REMOTE_OUT}" --include-per-engine > "${REMOTE_OUT}/budget-suggestions.env" || true
if [ "${ARTIFACT_MODE}" = full ]; then
  tar czf /root/apps-matrix-out.tgz -C /root apps-matrix-out
else
  python3 tests/e2e/apps-matrix/lib/collect_artifact_manifest.py --out-dir "${REMOTE_OUT}" --mode summary > "${REMOTE_OUT}/artifact-manifest.txt"
  rm -rf /root/apps-matrix-transfer
  mkdir -p /root/apps-matrix-transfer/apps-matrix-out
  python3 tests/e2e/apps-matrix/lib/collect_artifact_manifest.py \
    --out-dir "${REMOTE_OUT}" \
    --mode summary \
    --copy-to /root/apps-matrix-transfer/apps-matrix-out \
    > /dev/null
  tar czf /root/apps-matrix-out.tgz -C /root/apps-matrix-transfer apps-matrix-out
fi
exit "${matrix_status}"
REMOTE_SCRIPT
cat > "${LOCAL_REMOTE_LAUNCHER}" <<'REMOTE_LAUNCHER'
#!/usr/bin/env bash
set -euo pipefail
rm -f /root/apps-matrix-run.exit /root/apps-matrix-run.log
nohup bash -c 'bash /root/apps-matrix-run.sh > /root/apps-matrix-run.log 2>&1; printf "%s\n" "$?" > /root/apps-matrix-run.exit' >/dev/null 2>&1 &
REMOTE_LAUNCHER

scp_retry "upload remote env" "${LOCAL_REMOTE_ENV}" "root@${ip}:/root/apps-matrix-run.env" >/dev/null || \
    fail "failed to upload remote env"
scp_retry "upload remote script" "${LOCAL_REMOTE_SCRIPT}" "root@${ip}:/root/apps-matrix-run.sh" >/dev/null || \
    fail "failed to upload remote script"
scp_retry "upload remote launcher" "${LOCAL_REMOTE_LAUNCHER}" "root@${ip}:/root/apps-matrix-launch.sh" >/dev/null || \
    fail "failed to upload remote launcher"
ssh_retry "chmod remote launcher" "root@${ip}" "chmod 600 /root/apps-matrix-run.env && chmod +x /root/apps-matrix-run.sh /root/apps-matrix-launch.sh" || \
    fail "failed to chmod remote launcher"
remote_status=0
ssh_retry "launch remote run" "root@${ip}" "bash /root/apps-matrix-launch.sh" || \
    fail "failed to launch remote apps-matrix run"

log "polling remote apps-matrix run"
deadline=$(( $(date +%s) + RUN_TIMEOUT_SECONDS ))
while true; do
    if ssh "${ssh_opts[@]}" "root@${ip}" "test -f /root/apps-matrix-run.exit" >/dev/null 2>&1; then
        remote_status="$(ssh "${ssh_opts[@]}" "root@${ip}" "cat /root/apps-matrix-run.exit")"
        break
    fi
    if (( $(date +%s) >= deadline )); then
        remote_status=124
        ssh "${ssh_opts[@]}" "root@${ip}" "pkill -f /root/apps-matrix-run.sh || true" >/dev/null 2>&1 || true
        break
    fi
    ssh "${ssh_opts[@]}" "root@${ip}" "tail -n 80 /root/apps-matrix-run.log 2>/dev/null || true" || \
        log "remote poll failed; retrying"
    sleep 30
done

log "fetching artifacts"
scp_retry "fetch remote run log" "root@${ip}:/root/apps-matrix-run.log" "${OUT_ROOT}/apps-matrix-run.log" >/dev/null 2>&1 || true
if scp_retry "fetch artifact archive" "root@${ip}:/root/apps-matrix-out.tgz" "${OUT_ROOT}/apps-matrix-out.tgz"; then
    tar xzf "${OUT_ROOT}/apps-matrix-out.tgz" -C "${OUT_ROOT}"
elif [[ "${remote_status}" -eq 0 ]]; then
    fail "remote apps-matrix run succeeded but artifact archive was not created"
else
    fail "remote apps-matrix run failed with status ${remote_status}; artifact archive was not created"
fi

log "artifacts written to ${OUT_ROOT}"
if [[ "${remote_status}" -ne 0 ]]; then
    fail "remote apps-matrix run failed with status ${remote_status}; artifacts were fetched"
fi
