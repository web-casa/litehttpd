#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_ROOT="${APPS_MATRIX_DO_BASE_OUT:-${ROOT_DIR}/tests/e2e/apps-matrix/out/do-base}"
NAME="${APPS_MATRIX_DO_BASE_NAME:-apps-matrix-base-$(date +%Y%m%d%H%M%S)}"
REGION="${APPS_MATRIX_DO_BASE_REGION:-sfo3}"
SIZE="${APPS_MATRIX_DO_BASE_SIZE:-s-4vcpu-8gb}"
IMAGE="${APPS_MATRIX_DO_BASE_IMAGE:-ubuntu-24-04-x64}"
SNAPSHOT_NAME="${APPS_MATRIX_DO_BASE_SNAPSHOT_NAME:-${NAME}-snapshot}"
CREATE_SNAPSHOT="${APPS_MATRIX_DO_BASE_CREATE_SNAPSHOT:-1}"
KEEP_DROPLET="${APPS_MATRIX_DO_BASE_KEEP_DROPLET:-0}"
PREPULL_CORE_IMAGES="${APPS_MATRIX_DO_BASE_PREPULL_CORE_IMAGES:-1}"
APACHE_IMAGE="${APPS_MATRIX_DO_APACHE_IMAGE:-}"
OLS_MODULE_IMAGE="${APPS_MATRIX_DO_OLS_MODULE_IMAGE:-}"
DOCKER_REGISTRY="${APPS_MATRIX_DO_DOCKER_REGISTRY:-}"
DOCKER_USERNAME="${APPS_MATRIX_DO_DOCKER_USERNAME:-}"
DOCKER_PASSWORD="${APPS_MATRIX_DO_DOCKER_PASSWORD:-}"
SSH_KEY_PATH="${APPS_MATRIX_DO_BASE_SSH_KEY:-${OUT_ROOT}/${NAME}_id_ed25519}"
GENERATED_SSH_KEY=0
DROPLET_ID=""
SSH_KEY_ID=""

usage() {
    cat <<EOF
Usage: $0

Creates a disposable DigitalOcean droplet, installs apps-matrix base
dependencies, optionally pre-pulls runtime images, snapshots it, and destroys
the droplet by default.

Environment:
  APPS_MATRIX_DO_BASE_REGION           Default: sfo3
  APPS_MATRIX_DO_BASE_SIZE             Default: s-4vcpu-8gb
  APPS_MATRIX_DO_BASE_IMAGE            Default: ubuntu-24-04-x64
  APPS_MATRIX_DO_BASE_SNAPSHOT_NAME    Default: <name>-snapshot
  APPS_MATRIX_DO_BASE_CREATE_SNAPSHOT  0 or 1. Default: 1
  APPS_MATRIX_DO_BASE_KEEP_DROPLET     0 or 1. Default: 0
  APPS_MATRIX_DO_BASE_PREPULL_CORE_IMAGES 0 or 1. Default: 1
  APPS_MATRIX_DO_APACHE_IMAGE          Optional prebuilt Apache runtime image to pull
  APPS_MATRIX_DO_OLS_MODULE_IMAGE      Optional prebuilt ols_module runtime image to pull
  APPS_MATRIX_DO_DOCKER_REGISTRY       Optional Docker registry for private image login
  APPS_MATRIX_DO_DOCKER_USERNAME       Optional Docker registry username
  APPS_MATRIX_DO_DOCKER_PASSWORD       Optional Docker registry password or token

After the snapshot is created, use its printed snapshot ID as:
  APPS_MATRIX_DO_IMAGE=<snapshot-id>
  APPS_MATRIX_DO_SKIP_DEP_INSTALL=1
EOF
}

log() {
    printf '==> %s\n' "$*"
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "missing required tool: $1"
}

shell_quote() {
    printf '%q' "$1"
}

doctl_cmd() {
    if [[ -n "${DIGITALOCEAN_ACCESS_TOKEN:-}" ]]; then
        doctl --access-token "${DIGITALOCEAN_ACCESS_TOKEN}" "$@"
    else
        doctl "$@"
    fi
}

cleanup() {
    if [[ "${KEEP_DROPLET}" != "1" && -n "${DROPLET_ID}" ]]; then
        log "destroying droplet ${DROPLET_ID}"
        doctl_cmd compute droplet delete "${DROPLET_ID}" --force >/dev/null 2>&1 || true
    elif [[ -n "${DROPLET_ID}" ]]; then
        log "keeping droplet ${DROPLET_ID}"
    fi
    if [[ -n "${SSH_KEY_ID}" ]]; then
        log "removing temporary DigitalOcean SSH key ${SSH_KEY_ID}"
        doctl_cmd compute ssh-key delete "${SSH_KEY_ID}" --force >/dev/null 2>&1 || true
    fi
    if [[ "${GENERATED_SSH_KEY}" == "1" ]]; then
        rm -f "${SSH_KEY_PATH}" "${SSH_KEY_PATH}.pub"
    fi
}
trap cleanup EXIT

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

case "${CREATE_SNAPSHOT}" in 0|1) ;; *) fail "APPS_MATRIX_DO_BASE_CREATE_SNAPSHOT must be 0 or 1" ;; esac
case "${KEEP_DROPLET}" in 0|1) ;; *) fail "APPS_MATRIX_DO_BASE_KEEP_DROPLET must be 0 or 1" ;; esac
case "${PREPULL_CORE_IMAGES}" in 0|1) ;; *) fail "APPS_MATRIX_DO_BASE_PREPULL_CORE_IMAGES must be 0 or 1" ;; esac
if [[ "${CREATE_SNAPSHOT}" == "0" && "${KEEP_DROPLET}" != "1" ]]; then
    fail "APPS_MATRIX_DO_BASE_CREATE_SNAPSHOT=0 requires APPS_MATRIX_DO_BASE_KEEP_DROPLET=1"
fi
if [[ ! "${NAME}" =~ ^[A-Za-z0-9._-]+$ ]]; then
    fail "APPS_MATRIX_DO_BASE_NAME contains unsupported characters"
fi
if [[ ! "${SNAPSHOT_NAME}" =~ ^[A-Za-z0-9._-]+$ ]]; then
    fail "APPS_MATRIX_DO_BASE_SNAPSHOT_NAME contains unsupported characters"
fi
if [[ -n "${APACHE_IMAGE}" && ! "${APACHE_IMAGE}" =~ ^[A-Za-z0-9._:@/-]+$ ]]; then
    fail "APPS_MATRIX_DO_APACHE_IMAGE contains unsupported characters"
fi
if [[ -n "${OLS_MODULE_IMAGE}" && ! "${OLS_MODULE_IMAGE}" =~ ^[A-Za-z0-9._:@/-]+$ ]]; then
    fail "APPS_MATRIX_DO_OLS_MODULE_IMAGE contains unsupported characters"
fi
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

require_tool doctl
require_tool ssh
require_tool ssh-keygen

mkdir -p "${OUT_ROOT}"
chmod 700 "${OUT_ROOT}"

if [[ ! -f "${SSH_KEY_PATH}" ]]; then
    log "creating temporary SSH key ${SSH_KEY_PATH}"
    ssh-keygen -t ed25519 -N "" -f "${SSH_KEY_PATH}" -C "apps-matrix-do-base" >/dev/null
    GENERATED_SSH_KEY=1
fi

log "registering SSH key"
SSH_KEY_ID="$(doctl_cmd compute ssh-key import "${NAME}-ssh" \
    --public-key-file "${SSH_KEY_PATH}.pub" \
    --format ID \
    --no-header)"

log "creating droplet ${NAME} from ${IMAGE}"
DROPLET_ID="$(doctl_cmd compute droplet create "${NAME}" \
    --region "${REGION}" \
    --size "${SIZE}" \
    --image "${IMAGE}" \
    --ssh-keys "${SSH_KEY_ID}" \
    --tag-names apps-matrix,temporary,apps-matrix-base \
    --enable-monitoring \
    --wait \
    --format ID \
    --no-header | awk 'NF { print $1; exit }')"
[[ -n "${DROPLET_ID}" ]] || fail "droplet create did not return an ID"

log "waiting for droplet IPv4"
ip=""
for _ in $(seq 1 90); do
    ip="$(doctl_cmd compute droplet get "${DROPLET_ID}" \
        --format PublicIPv4 \
        --no-header | awk 'NF { print $1; exit }')"
    [[ -n "${ip}" ]] && break
    sleep 5
done
[[ -n "${ip}" ]] || fail "droplet did not receive a public IPv4"

ssh_opts=(-i "${SSH_KEY_PATH}" -o StrictHostKeyChecking=no -o UserKnownHostsFile="${OUT_ROOT}/known_hosts" -o ConnectTimeout=10)
log "waiting for SSH at ${ip}"
for _ in $(seq 1 90); do
    if ssh "${ssh_opts[@]}" "root@${ip}" true >/dev/null 2>&1; then
        break
    fi
    sleep 5
done
ssh "${ssh_opts[@]}" "root@${ip}" true >/dev/null 2>&1 || fail "SSH did not become ready"

log "installing base dependencies"
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
apt-get install -y ca-certificates curl git rsync tar gzip bzip2 unzip composer default-mysql-client php-cli php-curl php-gd php-intl php-mbstring php-mysql php-xml php-zip
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
for ext in curl gd intl mysqli pdo_mysql zip; do
  php -m | grep -Eix "$ext"
done
'

if [[ -n "${DOCKER_USERNAME}" ]]; then
    registry_label="${DOCKER_REGISTRY:-Docker Hub}"
    log "logging in to Docker registry ${registry_label}"
    registry_arg=""
    if [[ -n "${DOCKER_REGISTRY}" ]]; then
        registry_arg="$(shell_quote "${DOCKER_REGISTRY}")"
    fi
    # shellcheck disable=SC2029
    printf '%s' "${DOCKER_PASSWORD}" | ssh "${ssh_opts[@]}" "root@${ip}" \
        "docker login ${registry_arg} --username $(shell_quote "${DOCKER_USERNAME}") --password-stdin" >/dev/null
fi

if [[ "${PREPULL_CORE_IMAGES}" == "1" ]]; then
    log "pre-pulling core compose images"
    ssh "${ssh_opts[@]}" "root@${ip}" \
        "docker pull mariadb:10.11 >/dev/null && docker pull litespeedtech/openlitespeed:latest >/dev/null"
fi

if [[ -n "${APACHE_IMAGE}" ]]; then
    log "pre-pulling Apache runtime image ${APACHE_IMAGE}"
    # shellcheck disable=SC2029  # Image reference is intentionally client-quoted before remote execution.
    ssh "${ssh_opts[@]}" "root@${ip}" "docker pull $(shell_quote "${APACHE_IMAGE}")" >/dev/null
fi

if [[ -n "${OLS_MODULE_IMAGE}" ]]; then
    log "pre-pulling OLS module image ${OLS_MODULE_IMAGE}"
    # shellcheck disable=SC2029  # Image reference is intentionally client-quoted before remote execution.
    ssh "${ssh_opts[@]}" "root@${ip}" "docker pull $(shell_quote "${OLS_MODULE_IMAGE}")" >/dev/null
fi

if [[ "${CREATE_SNAPSHOT}" == "1" ]]; then
    log "powering off droplet before snapshot"
    ssh "${ssh_opts[@]}" "root@${ip}" "sync; systemctl stop docker >/dev/null 2>&1 || true" >/dev/null 2>&1 || true
    doctl_cmd compute droplet-action power-off "${DROPLET_ID}" --wait >/dev/null

    log "creating snapshot ${SNAPSHOT_NAME}"
    doctl_cmd compute droplet-action snapshot "${DROPLET_ID}" \
        --snapshot-name "${SNAPSHOT_NAME}" \
        --wait >/dev/null

    snapshot_id="$(doctl_cmd compute image list-user \
        --format ID,Name \
        --no-header | awk -v name="${SNAPSHOT_NAME}" '$2 == name { print $1; exit }')"
    log "snapshot ready: name=${SNAPSHOT_NAME} id=${snapshot_id:-unknown}"
    printf 'APPS_MATRIX_DO_IMAGE=%s\n' "${snapshot_id:-${SNAPSHOT_NAME}}"
    printf 'APPS_MATRIX_DO_SKIP_DEP_INSTALL=1\n'
else
    log "base droplet prepared without snapshot"
fi
