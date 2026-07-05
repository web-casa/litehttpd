#!/usr/bin/env bash
# Dispatch and optionally watch the final apps-matrix DigitalOcean release gate.
# This goes through nightly.yml so the runtime images are built once in GHCR and
# the DO job can run with MATRIX_COMPOSE_BUILD=0.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

WORKFLOW="${APPS_MATRIX_FINAL_DO_WORKFLOW:-nightly.yml}"
REF="${APPS_MATRIX_FINAL_DO_REF:-}"
REPO="${APPS_MATRIX_FINAL_DO_REPO:-}"
SCENARIOS="${APPS_MATRIX_FINAL_DO_SCENARIOS:-heavy}"
DROPLET_IMAGE="${APPS_MATRIX_FINAL_DO_DROPLET_IMAGE:-${APPS_MATRIX_DO_IMAGE:-}}"
SKIP_DEP_INSTALL="${APPS_MATRIX_FINAL_DO_SKIP_DEP_INSTALL:-${APPS_MATRIX_DO_SKIP_DEP_INSTALL:-0}}"
CONTINUE_ON_FAILURE="${APPS_MATRIX_FINAL_DO_CONTINUE_ON_FAILURE:-0}"
WATCH="${APPS_MATRIX_FINAL_DO_WATCH:-1}"
CHECK_DO_CLEANUP="${APPS_MATRIX_FINAL_DO_CHECK_CLEANUP:-1}"
GH_REPO_ARGS=()

usage() {
    cat <<'USAGE'
Usage: bash scripts/apps-matrix-final-do-gate.sh [options]

Dispatches nightly.yml with run_do_release_gate=1. The nightly workflow builds
and pushes the apps-matrix runtime images, then calls the DO release gate with
those exact image refs so the VPS does not rebuild runtimes.

Options:
  --repo OWNER/REPO          GitHub repository. Default: private remote when
                            present, otherwise gh infers it.
  --ref REF                 Branch, tag, or SHA. Default: main unless the
                            current branch is main/master.
  --workflow FILE           Workflow file. Default: nightly.yml.
  --scenarios LIST          heavy, wordpress, cms, framework, all, or scenario.
                            Default: heavy.
  --droplet-image IMAGE     DO image slug or prepared snapshot ID.
  --skip-dep-install 0|1    Use 1 with prepared DO snapshots. Default: 0.
  --continue-on-failure 0|1 Default: 0.
  --watch 0|1               Watch run and return its status. Default: 1.
  --check-cleanup 0|1       After watched runs, require no DO droplets. Default: 1.

Environment mirrors the option names with APPS_MATRIX_FINAL_DO_* prefixes.
USAGE
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "missing required tool: $1"
}

github_repo_from_remote() {
    local remote="$1"
    local url path

    url="$(git remote get-url "$remote" 2>/dev/null || true)"
    [ -n "$url" ] || return 0

    case "$url" in
        https://github.com/*)
            path="${url#https://github.com/}"
            ;;
        git@github.com:*)
            path="${url#git@github.com:}"
            ;;
        ssh://git@github.com/*)
            path="${url#ssh://git@github.com/}"
            ;;
        *)
            return 0
            ;;
    esac
    path="${path%.git}"
    if printf '%s' "$path" | grep -Eq '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$'; then
        printf '%s\n' "$path"
    fi
}

validate_bool() {
    local name="$1"
    local value="$2"

    case "$value" in
        0|1) ;;
        *) fail "$name must be 0 or 1, got: $value" ;;
    esac
}

validate_scenarios() {
    local scenarios
    local scenario

    scenarios="$(printf '%s\n' "$1" | tr ', ' '\n' | sed '/^$/d')"
    [ -n "$scenarios" ] || fail "at least one scenario is required"

    while IFS= read -r scenario; do
        case "$scenario" in
            all|heavy|wordpress|cms|framework|wordpress-core|wordpress-redirection|wordpress-w3-total-cache|wordpress-litespeed-cache|wordpress-wordfence|wordpress-ewww|wordpress-wp-optimize|laravel|drupal|nextcloud|joomla|mediawiki) ;;
            *) fail "unsupported scenario: $scenario" ;;
        esac
    done <<< "$scenarios"
}

default_ref() {
    local branch

    branch="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || true)"
    case "$branch" in
        main|master)
            printf '%s\n' "$branch"
            ;;
        *)
            printf '%s\n' main
            ;;
    esac
}

normalize_ref() {
    local ref="$1"

    if [ -z "$ref" ] || [ "$ref" = "HEAD" ]; then
        default_ref
        return 0
    fi
    case "$ref" in
        private/*|origin/*)
            printf '%s\n' "${ref#*/}"
            ;;
        *)
            printf '%s\n' "$ref"
            ;;
    esac
}

resolve_ref_sha() {
    local ref="$1"
    local candidate remote_sha

    if printf '%s' "$ref" | grep -Eq '^[0-9a-f]{40}$'; then
        printf '%s\n' "$ref"
        return 0
    fi

    if [ -n "$REPO" ]; then
        remote_sha="$(gh api "repos/$REPO/commits/$ref" --jq '.sha' 2>/dev/null || true)"
        if printf '%s' "$remote_sha" | grep -Eq '^[0-9a-f]{40}$'; then
            printf '%s\n' "$remote_sha"
            return 0
        fi
    fi

    for candidate in "private/$ref" "origin/$ref" "$ref"; do
        if git rev-parse --verify "${candidate}^{commit}" >/dev/null 2>&1; then
            git rev-parse --verify "${candidate}^{commit}"
            return 0
        fi
    done

    return 1
}

parse_run_id() {
    local output="$1"
    local run_id

    run_id="$(printf '%s\n' "$output" | sed -nE 's#.*/runs/([0-9]+).*#\1#p' | head -1)"
    if [ -n "$run_id" ]; then
        printf '%s\n' "$run_id"
        return 0
    fi

    printf '%s\n' "$output" | sed -nE 's#^([0-9]+)$#\1#p' | head -1
}

wait_for_dispatched_run_id() {
    local branch="$1"
    local workflow="$2"
    local created_after="$3"
    local head_sha="$4"
    local run_id
    local head_filter='true'

    if [ -n "$head_sha" ]; then
        head_filter=".headSha == \"$head_sha\""
    fi

    for _ in $(seq 1 24); do
        run_id="$(
            gh run list "${GH_REPO_ARGS[@]}" \
                --workflow "$workflow" \
                --branch "$branch" \
                --limit 10 \
                --json databaseId,event,status,createdAt,headSha \
                --jq "([.[] | select(.event == \"workflow_dispatch\" and .createdAt >= \"$created_after\" and $head_filter and (.status == \"queued\" or .status == \"in_progress\" or .status == \"completed\")) | .databaseId] | first // empty)"
        )"
        if [ -n "$run_id" ]; then
            printf '%s\n' "$run_id"
            return 0
        fi
        sleep 5
    done

    return 1
}

run_has_billing_blocker() {
    local run_id="$1"
    local job_ids job_id message

    job_ids="$(gh run view "$run_id" "${GH_REPO_ARGS[@]}" --json jobs --jq '.jobs[].databaseId')"
    while IFS= read -r job_id; do
        [ -n "$job_id" ] || continue
        message="$(
            gh api "repos/$REPO/check-runs/$job_id/annotations" --paginate \
                --jq '.[].message' 2>/dev/null || true
        )"
        if printf '%s\n' "$message" |
            grep -Eiq 'spending limit|payments have failed|Billing & plans'; then
            return 0
        fi
    done <<< "$job_ids"

    return 1
}

describe_run_failure() {
    local run_id="$1"

    if [ -n "$REPO" ] && run_has_billing_blocker "$run_id"; then
        printf 'ERROR: GitHub Actions billing/payment/spending-limit prevented DO release gate jobs from starting.\n' >&2
        return 0
    fi

    printf 'ERROR: GitHub DO release gate run failed: %s\n' "$run_id" >&2
    gh run view "$run_id" "${GH_REPO_ARGS[@]}" --json url,status,conclusion,jobs \
        --jq '.url, (.jobs[] | "  \(.name): status=\(.status) conclusion=\(.conclusion // "none")")' >&2 || true
}

check_do_cleanup() {
    local droplets

    command -v doctl >/dev/null 2>&1 || fail "doctl is required to verify DO cleanup"
    if ! droplets="$(doctl compute droplet list --format ID,Name,Status,PublicIPv4 --no-header 2>&1)"; then
        printf '%s\n' "$droplets" >&2
        fail "could not list DigitalOcean droplets after release gate"
    fi
    if [ -n "$droplets" ]; then
        printf '%s\n' "$droplets" >&2
        fail "DigitalOcean droplets are still present after release gate"
    fi
    printf 'OK: no DigitalOcean droplets remain.\n'
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --repo)
            [ "$#" -ge 2 ] || fail "--repo requires a value"
            REPO="$2"
            shift
            ;;
        --ref)
            [ "$#" -ge 2 ] || fail "--ref requires a value"
            REF="$2"
            shift
            ;;
        --workflow)
            [ "$#" -ge 2 ] || fail "--workflow requires a value"
            WORKFLOW="$2"
            shift
            ;;
        --scenarios)
            [ "$#" -ge 2 ] || fail "--scenarios requires a value"
            SCENARIOS="$2"
            shift
            ;;
        --droplet-image)
            [ "$#" -ge 2 ] || fail "--droplet-image requires a value"
            DROPLET_IMAGE="$2"
            shift
            ;;
        --skip-dep-install)
            [ "$#" -ge 2 ] || fail "--skip-dep-install requires a value"
            SKIP_DEP_INSTALL="$2"
            shift
            ;;
        --continue-on-failure)
            [ "$#" -ge 2 ] || fail "--continue-on-failure requires a value"
            CONTINUE_ON_FAILURE="$2"
            shift
            ;;
        --watch)
            [ "$#" -ge 2 ] || fail "--watch requires a value"
            WATCH="$2"
            shift
            ;;
        --check-cleanup)
            [ "$#" -ge 2 ] || fail "--check-cleanup requires a value"
            CHECK_DO_CLEANUP="$2"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
    shift
done

require_tool gh
validate_bool APPS_MATRIX_FINAL_DO_SKIP_DEP_INSTALL "$SKIP_DEP_INSTALL"
validate_bool APPS_MATRIX_FINAL_DO_CONTINUE_ON_FAILURE "$CONTINUE_ON_FAILURE"
validate_bool APPS_MATRIX_FINAL_DO_WATCH "$WATCH"
validate_bool APPS_MATRIX_FINAL_DO_CHECK_CLEANUP "$CHECK_DO_CLEANUP"
validate_scenarios "$SCENARIOS"

if [ -z "$REPO" ]; then
    REPO="$(github_repo_from_remote private)"
fi
if [ -n "$REPO" ] && ! printf '%s' "$REPO" | grep -Eq '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$'; then
    fail "GitHub repo must be OWNER/REPO, got: $REPO"
fi
if [ -n "$REPO" ]; then
    GH_REPO_ARGS=(--repo "$REPO")
fi

REF="$(normalize_ref "$REF")"
[ -n "$REF" ] || fail "could not resolve current git ref"
if ! printf '%s' "$REF" | grep -Eq '^[A-Za-z0-9._/@:-]+$'; then
    fail "ref contains unsupported characters: $REF"
fi

if [ -n "$DROPLET_IMAGE" ] && ! printf '%s' "$DROPLET_IMAGE" | grep -Eq '^[A-Za-z0-9._:-]+$'; then
    fail "droplet image contains unsupported characters: $DROPLET_IMAGE"
fi

dispatch_started="$(date -u -d '2 minutes ago' '+%Y-%m-%dT%H:%M:%SZ')"
head_sha="$(resolve_ref_sha "$REF" 2>/dev/null || true)"

printf 'Dispatching %s on %s with DO scenarios=%s repo=%s\n' \
    "$WORKFLOW" "$REF" "$SCENARIOS" "${REPO:-gh-default}"
dispatch_output="$(
    gh workflow run "$WORKFLOW" "${GH_REPO_ARGS[@]}" \
        --ref "$REF" \
        -f run_do_release_gate=1 \
        -f do_release_gate_scenarios="$SCENARIOS" \
        -f do_release_gate_droplet_image="$DROPLET_IMAGE" \
        -f do_release_gate_skip_dep_install="$SKIP_DEP_INSTALL" \
        -f do_release_gate_continue_on_failure="$CONTINUE_ON_FAILURE"
)"

if [ -n "$dispatch_output" ]; then
    printf '%s\n' "$dispatch_output"
fi

run_id="$(parse_run_id "$dispatch_output")"
if [ -z "$run_id" ]; then
    run_id="$(wait_for_dispatched_run_id "$REF" "$WORKFLOW" "$dispatch_started" "$head_sha")" ||
        fail "workflow dispatched, but could not resolve run id"
fi

printf 'GitHub run id: %s\n' "$run_id"

if [ "$WATCH" = "1" ]; then
    watch_rc=0
    gh run watch "$run_id" "${GH_REPO_ARGS[@]}" --exit-status || watch_rc=$?
    cleanup_rc=0
    if [ "$CHECK_DO_CLEANUP" = "1" ]; then
        ( check_do_cleanup ) || cleanup_rc=$?
    fi
    if [ "$watch_rc" -ne 0 ]; then
        describe_run_failure "$run_id"
        exit "$watch_rc"
    fi
    if [ "$cleanup_rc" -ne 0 ]; then
        exit "$cleanup_rc"
    fi
else
    printf 'Watch disabled. Inspect with: gh run view %s --web\n' "$run_id"
fi
