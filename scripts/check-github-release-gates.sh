#!/usr/bin/env bash
# Validate the remote GitHub gates that cannot be proven from a local checkout:
# Dependabot alerts, formal workflow results, and account billing blockers.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

REPO="${RELEASE_GITHUB_REPO:-${GH_REPO:-}}"
REF="${RELEASE_GITHUB_REF:-}"
SHA="${RELEASE_GITHUB_SHA:-}"
CHECK_DEPENDABOT="${RELEASE_GITHUB_CHECK_DEPENDABOT:-1}"
RERUN_FORMAL="${RELEASE_GITHUB_RERUN_FORMAL:-0}"
WATCH="${RELEASE_GITHUB_WATCH:-0}"
TIMEOUT_SECONDS="${RELEASE_GITHUB_TIMEOUT_SECONDS:-21600}"
POLL_SECONDS="${RELEASE_GITHUB_POLL_SECONDS:-30}"
GH_REPO_ARGS=()

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-github-release-gates.sh [options]

Checks remote release blockers that live in GitHub rather than the source tree:
open Dependabot alerts, formal CI workflow conclusions, and GitHub Actions
billing/spending-limit failures.

Options:
  --repo OWNER/REPO       GitHub repository. Default: GH_REPO, private remote,
                          then origin remote.
  --ref REF              Branch/tag/ref to inspect or dispatch. Default: main
                          unless current branch is main/master.
  --sha SHA              Expected commit SHA for workflow runs. Default:
                          resolved from --ref.
  --skip-dependabot      Do not query Dependabot alerts.
  --rerun-formal         Rerun failed CI/CodeQL runs and dispatch runtime/nightly.
  --watch                Poll required workflows until success/failure.
  --timeout-seconds N    Watch timeout. Default: 21600.
  --poll-seconds N       Watch poll interval. Default: 30.
  -h, --help             Show this help.

Environment mirrors the options with RELEASE_GITHUB_* variables.
USAGE
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "missing required tool: $1"
}

validate_bool() {
    local name="$1"
    local value="$2"

    case "$value" in
        0|1) ;;
        *) fail "$name must be 0 or 1, got: $value" ;;
    esac
}

validate_positive_int() {
    local name="$1"
    local value="$2"

    if ! printf '%s' "$value" | grep -Eq '^[1-9][0-9]*$'; then
        fail "$name must be a positive integer, got: $value"
    fi
}

validate_sha() {
    local label="$1"
    local value="$2"

    if ! printf '%s' "$value" | grep -Eq '^[0-9a-f]{40}$'; then
        fail "$label must be a full 40-character git SHA, got: $value"
    fi
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

default_ref() {
    local branch

    branch="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || true)"
    case "$branch" in
        main|master)
            printf '%s\n' "$branch"
            ;;
        *)
            printf '%s\n' "${RELEASE_GITHUB_DEFAULT_BRANCH:-main}"
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

    remote_sha="$(gh api "repos/$REPO/commits/$ref" --jq '.sha' 2>/dev/null || true)"
    if printf '%s' "$remote_sha" | grep -Eq '^[0-9a-f]{40}$'; then
        printf '%s\n' "$remote_sha"
        return 0
    fi

    for candidate in "private/$ref" "origin/$ref" "$ref"; do
        if git rev-parse --verify "${candidate}^{commit}" >/dev/null 2>&1; then
            git rev-parse --verify "${candidate}^{commit}"
            return 0
        fi
    done

    return 1
}

latest_run_tsv() {
    local workflow="$1"

    gh run list "${GH_REPO_ARGS[@]}" \
        --workflow "$workflow" \
        --limit 50 \
        --json databaseId,workflowName,headSha,status,conclusion,event,createdAt,url \
        --jq "([.[] | select(.headSha == \"$SHA\") | [.databaseId, .workflowName, .status, (.conclusion // \"\"), .event, .createdAt, .url]] | first // empty) | @tsv"
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

parse_run_id() {
    local output="$1"

    printf '%s\n' "$output" |
        sed -nE 's#.*/runs/([0-9]+).*#\1#p; s#^([0-9]+)$#\1#p' |
        head -1
}

wait_for_dispatched_run_id() {
    local workflow="$1"
    local created_after="$2"
    local run_id

    for _ in $(seq 1 24); do
        run_id="$(
            gh run list "${GH_REPO_ARGS[@]}" \
                --workflow "$workflow" \
                --branch "$REF" \
                --limit 20 \
                --json databaseId,event,status,createdAt,headSha \
                --jq "([.[] | select(.event == \"workflow_dispatch\" and .createdAt >= \"$created_after\" and .headSha == \"$SHA\" and (.status == \"queued\" or .status == \"in_progress\" or .status == \"completed\")) | .databaseId] | first // empty)"
        )"
        if [ -n "$run_id" ]; then
            printf '%s\n' "$run_id"
            return 0
        fi
        sleep 5
    done

    return 1
}

dispatch_workflow() {
    local workflow="$1"
    local kind="$2"
    local short_sha output run_id created_after

    short_sha="${SHA:0:12}"
    created_after="$(date -u -d '2 minutes ago' '+%Y-%m-%dT%H:%M:%SZ')"
    printf 'Dispatching %s on %s\n' "$workflow" "$REF"

    case "$kind" in
        runtime)
            output="$(
                gh workflow run "$workflow" "${GH_REPO_ARGS[@]}" \
                    --ref "$REF" \
                    -f image_tag="$short_sha" \
                    -f extra_tag=release-gate \
                    -f push_images=1 \
                    -f reuse_existing=0
            )"
            ;;
        nightly)
            output="$(
                gh workflow run "$workflow" "${GH_REPO_ARGS[@]}" \
                    --ref "$REF" \
                    -f run_do_release_gate=0 \
                    -f runtime_images_reuse_existing=1
            )"
            ;;
        codeql)
            output="$(gh workflow run "$workflow" "${GH_REPO_ARGS[@]}" --ref "$REF")"
            ;;
        *)
            fail "unsupported dispatch kind: $kind"
            ;;
    esac

    [ -z "$output" ] || printf '%s\n' "$output"
    run_id="$(parse_run_id "$output")"
    if [ -z "$run_id" ]; then
        run_id="$(wait_for_dispatched_run_id "$workflow" "$created_after")" ||
            fail "workflow dispatched, but could not resolve run id: $workflow"
    fi
    printf 'GitHub run id: %s\n' "$run_id"
}

maybe_rerun_or_dispatch() {
    local workflow="$1"
    local kind="$2"
    local tsv run_id

    [ "$RERUN_FORMAL" = "1" ] || return 0

    case "$kind" in
        rerun)
            tsv="$(latest_run_tsv "$workflow")"
            if [ -z "$tsv" ]; then
                printf 'WARN: no %s run exists for %s; cannot rerun push-only workflow\n' "$workflow" "$SHA" >&2
                return 0
            fi
            IFS=$'\t' read -r run_id _ < <(printf '%s\n' "$tsv")
            printf 'Rerunning %s run %s\n' "$workflow" "$run_id"
            gh run rerun "$run_id" "${GH_REPO_ARGS[@]}"
            ;;
        runtime|nightly|codeql)
            dispatch_workflow "$workflow" "$kind"
            ;;
        *)
            fail "unsupported rerun kind: $kind"
            ;;
    esac
}

check_dependabot() {
    local alerts

    [ "$CHECK_DEPENDABOT" = "1" ] || {
        printf 'SKIP: Dependabot alert check disabled.\n'
        return 0
    }

    alerts="$(
        gh api -H 'Accept: application/vnd.github+json' \
            "repos/$REPO/dependabot/alerts?state=open&per_page=100" \
            --jq 'length'
    )"
    if [ "$alerts" != "0" ]; then
        printf 'Open Dependabot alerts:\n' >&2
        gh api -H 'Accept: application/vnd.github+json' \
            "repos/$REPO/dependabot/alerts?state=open&per_page=100" \
            --jq '.[] | [.number, .dependency.manifest_path, .dependency.package.name, .security_vulnerability.severity, .security_advisory.ghsa_id] | @tsv' >&2
        fail "Dependabot has $alerts open alert(s)"
    fi

    printf 'OK: no open Dependabot alerts.\n'
}

check_workflows_once() {
    local failures=0
    local billing_blocked=0
    local pending=0
    local workflow label kind tsv run_id workflow_name status conclusion event created_at url

    while IFS='|' read -r workflow label kind; do
        [ -n "$workflow" ] || continue
        tsv="$(latest_run_tsv "$workflow")"
        if [ -z "$tsv" ]; then
            printf 'FAIL: %s has no run for %s\n' "$label" "$SHA" >&2
            failures=$((failures + 1))
            continue
        fi

        IFS=$'\t' read -r run_id workflow_name status conclusion event created_at url < <(printf '%s\n' "$tsv")
        printf '%s: run=%s status=%s conclusion=%s event=%s created=%s\n' \
            "$workflow_name" "$run_id" "$status" "${conclusion:-none}" "$event" "$created_at"
        printf '  %s\n' "$url"

        if [ "$status" != "completed" ]; then
            pending=$((pending + 1))
            continue
        fi
        if [ "$conclusion" = "success" ]; then
            continue
        fi

        if run_has_billing_blocker "$run_id"; then
            printf '  BLOCKED: GitHub Actions billing/payment/spending-limit prevented job start.\n' >&2
            billing_blocked=1
        fi
        failures=$((failures + 1))
    done <<'WORKFLOWS'
ci.yml|CI|rerun
codeql.yml|CodeQL|codeql
apps-matrix-runtime-images.yml|Apps Matrix Runtime Images|runtime
nightly.yml|Nightly|nightly
WORKFLOWS

    if [ "$billing_blocked" -eq 1 ]; then
        fail "GitHub Actions billing/spending-limit blocker is still active"
    fi
    if [ "$failures" -gt 0 ]; then
        fail "$failures required GitHub workflow gate(s) failed or are missing"
    fi
    if [ "$pending" -gt 0 ]; then
        return 75
    fi
    return 0
}

watch_workflows() {
    local deadline now rc

    deadline=$(( $(date +%s) + TIMEOUT_SECONDS ))
    while true; do
        set +e
        check_workflows_once
        rc=$?
        set -e
        case "$rc" in
            0)
                return 0
                ;;
            75)
                now="$(date +%s)"
                [ "$now" -lt "$deadline" ] || fail "timed out waiting for GitHub workflows"
                printf 'Waiting %ss for GitHub workflows to finish...\n' "$POLL_SECONDS"
                sleep "$POLL_SECONDS"
                ;;
            *)
                return "$rc"
                ;;
        esac
    done
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
        --sha)
            [ "$#" -ge 2 ] || fail "--sha requires a value"
            SHA="$2"
            shift
            ;;
        --skip-dependabot)
            CHECK_DEPENDABOT=0
            ;;
        --rerun-formal)
            RERUN_FORMAL=1
            ;;
        --watch)
            WATCH=1
            ;;
        --timeout-seconds)
            [ "$#" -ge 2 ] || fail "--timeout-seconds requires a value"
            TIMEOUT_SECONDS="$2"
            shift
            ;;
        --poll-seconds)
            [ "$#" -ge 2 ] || fail "--poll-seconds requires a value"
            POLL_SECONDS="$2"
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
validate_bool RELEASE_GITHUB_CHECK_DEPENDABOT "$CHECK_DEPENDABOT"
validate_bool RELEASE_GITHUB_RERUN_FORMAL "$RERUN_FORMAL"
validate_bool RELEASE_GITHUB_WATCH "$WATCH"
validate_positive_int RELEASE_GITHUB_TIMEOUT_SECONDS "$TIMEOUT_SECONDS"
validate_positive_int RELEASE_GITHUB_POLL_SECONDS "$POLL_SECONDS"

if [ -z "$REPO" ]; then
    REPO="$(github_repo_from_remote private)"
fi
if [ -z "$REPO" ]; then
    REPO="$(github_repo_from_remote origin)"
fi
[ -n "$REPO" ] || fail "could not resolve GitHub repo; pass --repo OWNER/REPO"
if ! printf '%s' "$REPO" | grep -Eq '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$'; then
    fail "GitHub repo must be OWNER/REPO, got: $REPO"
fi
GH_REPO_ARGS=(--repo "$REPO")

if [ -z "$REF" ] || [ "$REF" = "HEAD" ]; then
    REF="$(default_ref)"
fi
[ -n "$REF" ] || fail "could not resolve release ref"
case "$REF" in
    private/*|origin/*)
        REF="${REF#*/}"
        ;;
esac
if ! printf '%s' "$REF" | grep -Eq '^[A-Za-z0-9._/@:-]+$'; then
    fail "release ref contains unsupported characters: $REF"
fi

if [ -z "$SHA" ]; then
    SHA="$(resolve_ref_sha "$REF")"
fi
validate_sha "release SHA" "$SHA"

printf 'GitHub release gates: repo=%s ref=%s sha=%s\n' "$REPO" "$REF" "$SHA"

check_dependabot

while IFS='|' read -r workflow _ kind; do
    [ -n "$workflow" ] || continue
    maybe_rerun_or_dispatch "$workflow" "$kind"
done <<'WORKFLOWS'
ci.yml|CI|rerun
codeql.yml|CodeQL|codeql
apps-matrix-runtime-images.yml|Apps Matrix Runtime Images|runtime
nightly.yml|Nightly|nightly
WORKFLOWS

if [ "$WATCH" = "1" ]; then
    watch_workflows
else
    check_workflows_once
fi

printf 'OK: GitHub release gates are green for %s.\n' "$SHA"
