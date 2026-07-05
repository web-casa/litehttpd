#!/usr/bin/env bash
# Final pre-tag release gate. This validates the exact remote commit that would
# be tagged, using a clean temporary worktree so local dirty files cannot leak
# into release packaging.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

REPO="${RELEASE_TAG_REPO:-${GH_REPO:-}}"
REF="${RELEASE_TAG_REF:-main}"
TAG="${RELEASE_TAG_NAME:-}"
CHECK_GITHUB_GATES="${RELEASE_TAG_CHECK_GITHUB_GATES:-1}"
REQUIRE_DO_GATE="${RELEASE_TAG_REQUIRE_DO_GATE:-0}"
ALLOW_EXISTING_TAG="${RELEASE_TAG_ALLOW_EXISTING:-0}"
CREATE_TAG="${RELEASE_TAG_CREATE:-0}"
PUSH_TAG="${RELEASE_TAG_PUSH:-0}"
KEEP_WORKTREE="${RELEASE_TAG_KEEP_WORKTREE:-0}"
MANIFEST_DIR="${RELEASE_TAG_MANIFEST_DIR:-out/release-tag-gate}"
WORKTREE_DIR="${RELEASE_TAG_WORKTREE_DIR:-}"
GH_REPO_ARGS=()
WORKTREE_CREATED=0

usage() {
    cat <<'USAGE'
Usage: bash scripts/check-release-tag-gate.sh [options]

Validates that a release tag can be safely cut from a clean checkout of the
selected remote ref. By default it does not create or push a tag.

Options:
  --repo OWNER/REPO       GitHub repository. Default: GH_REPO, private remote,
                          then origin remote.
  --ref REF              Branch, tag, or SHA to release. Default: main.
  --tag vX.Y.Z           Release tag. Default: derived from package metadata.
  --skip-github-gates    Skip formal GitHub CI gate check.
  --require-do-gate      Require a successful Nightly run with the
                          apps-matrix-do-release-gate artifact for the SHA.
  --allow-existing-tag   Do not fail if the tag already exists at the same SHA.
  --create-tag           Create an annotated local tag after checks pass.
  --push-tag             Push the tag to the private remote after checks pass.
                          Implies --create-tag.
  --manifest-dir DIR     Write release-tag manifests. Default:
                          out/release-tag-gate.
  --worktree-dir DIR     Use this clean worktree path instead of a temp path.
  --keep-worktree        Do not remove the temporary worktree.
  -h, --help             Show this help.

Environment mirrors options with RELEASE_TAG_* variables.
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

validate_sha() {
    local label="$1"
    local value="$2"

    if ! printf '%s' "$value" | grep -Eq '^[0-9a-f]{40}$'; then
        fail "$label must be a full 40-character git SHA, got: $value"
    fi
}

validate_tag() {
    local tag="$1"

    case "$tag" in
        v[0-9]*.[0-9]*.[0-9]*) ;;
        *) fail "release tag must look like vX.Y.Z, got: $tag" ;;
    esac
    if ! printf '%s' "$tag" | grep -Eq '^v[0-9]+[.][0-9]+[.][0-9]+([._+-][A-Za-z0-9.-]+)?$'; then
        fail "release tag contains unsupported characters: $tag"
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

normalize_ref() {
    local ref="$1"

    if [ -z "$ref" ] || [ "$ref" = "HEAD" ]; then
        printf '%s\n' main
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

cleanup() {
    if [ "$WORKTREE_CREATED" = "1" ] && [ "$KEEP_WORKTREE" = "0" ]; then
        git worktree remove --force "$WORKTREE_DIR" >/dev/null 2>&1 || true
    fi
}

prepare_worktree() {
    local sha="$1"

    if [ -z "$WORKTREE_DIR" ]; then
        WORKTREE_DIR="$(mktemp -d /tmp/litehttpd-release-tag.XXXXXX)"
        rmdir "$WORKTREE_DIR"
    elif [ -e "$WORKTREE_DIR" ]; then
        fail "worktree path already exists: $WORKTREE_DIR"
    fi

    mkdir -p "$(dirname "$WORKTREE_DIR")"
    git worktree add --detach "$WORKTREE_DIR" "$sha" >/dev/null
    WORKTREE_CREATED=1
}

derive_tag_from_worktree() {
    local version

    version="$(
        awk '/^%global[[:space:]]+litehttpd_version[[:space:]]+/ {print $3; exit}' \
            "$WORKTREE_DIR/packaging/openlitespeed-litehttpd.spec"
    )"
    [ -n "$version" ] || fail "could not derive release version from worktree"
    printf 'v%s\n' "$version"
}

remote_tag_sha() {
    local tag="$1"
    local object_sha object_type peeled_sha

    object_sha="$(gh api "repos/$REPO/git/ref/tags/$tag" --jq '.object.sha' 2>/dev/null || true)"
    object_type="$(gh api "repos/$REPO/git/ref/tags/$tag" --jq '.object.type' 2>/dev/null || true)"
    [ -n "$object_sha" ] || return 0

    if [ "$object_type" = "tag" ]; then
        peeled_sha="$(
            gh api "repos/$REPO/git/tags/$object_sha" --jq '.object.sha' 2>/dev/null || true
        )"
        if printf '%s' "$peeled_sha" | grep -Eq '^[0-9a-f]{40}$'; then
            printf '%s\n' "$peeled_sha"
            return 0
        fi
    fi

    printf '%s\n' "$object_sha"
}

local_tag_sha() {
    local tag="$1"

    git rev-parse -q --verify "refs/tags/$tag^{commit}" 2>/dev/null || true
}

check_tag_available() {
    local tag="$1"
    local sha="$2"
    local existing_remote existing_local

    existing_remote="$(remote_tag_sha "$tag")"
    if [ -n "$existing_remote" ]; then
        if [ "$ALLOW_EXISTING_TAG" = "1" ] && [ "$existing_remote" = "$sha" ]; then
            printf 'OK: remote tag %s already points at %s.\n' "$tag" "$sha"
        else
            fail "remote tag $tag already exists at $existing_remote"
        fi
    fi

    existing_local="$(local_tag_sha "$tag")"
    if [ -n "$existing_local" ]; then
        if [ "$ALLOW_EXISTING_TAG" = "1" ] && [ "$existing_local" = "$sha" ]; then
            printf 'OK: local tag %s already points at %s.\n' "$tag" "$sha"
        else
            fail "local tag $tag already exists at $existing_local"
        fi
    fi
}

check_do_release_gate_artifact() {
    local sha="$1"
    local run_ids run_id artifact_names run_url

    run_ids="$(
        gh run list "${GH_REPO_ARGS[@]}" \
            --workflow nightly.yml \
            --limit 50 \
            --json databaseId,headSha,status,conclusion \
            --jq ".[] | select(.headSha == \"$sha\" and .status == \"completed\" and .conclusion == \"success\") | .databaseId"
    )"

    while IFS= read -r run_id; do
        [ -n "$run_id" ] || continue
        artifact_names="$(
            gh api "repos/$REPO/actions/runs/$run_id/artifacts" \
                --jq '.artifacts[].name' 2>/dev/null || true
        )"
        if printf '%s\n' "$artifact_names" | grep -Fxq apps-matrix-do-release-gate; then
            run_url="$(gh run view "$run_id" "${GH_REPO_ARGS[@]}" --json url --jq '.url')"
            printf 'OK: DO release gate artifact found on Nightly run %s\n' "$run_id"
            printf '  %s\n' "$run_url"
            return 0
        fi
    done <<< "$run_ids"

    fail "no successful Nightly run with apps-matrix-do-release-gate artifact found for $sha"
}

run_clean_worktree_checks() {
    local sha="$1"
    local tag="$2"
    local clean_manifest="$MANIFEST_DIR/clean-worktree"

    (
        cd "$WORKTREE_DIR"
        bash scripts/check-release-source-isolated.sh \
            --ref HEAD \
            --expect-sha "$sha" \
            --manifest-dir "$clean_manifest/source"
        bash scripts/check-release-metadata.sh \
            --tag "$tag" \
            --manifest-dir "$clean_manifest/metadata"
        bash scripts/check-release-matrix.sh
        bash scripts/check-apps-matrix-budget-policy.sh
    )
}

create_or_push_tag() {
    local tag="$1"
    local sha="$2"

    [ "$CREATE_TAG" = "1" ] || return 0

    if [ -n "$(local_tag_sha "$tag")" ]; then
        printf 'Local tag %s already exists; not recreating it.\n' "$tag"
    else
        git tag -a "$tag" "$sha" -m "litehttpd $tag"
        printf 'Created local annotated tag %s at %s.\n' "$tag" "$sha"
    fi

    if [ "$PUSH_TAG" = "1" ]; then
        git push private "refs/tags/$tag"
        printf 'Pushed tag %s to private.\n' "$tag"
    fi
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
        --tag)
            [ "$#" -ge 2 ] || fail "--tag requires a value"
            TAG="$2"
            shift
            ;;
        --skip-github-gates)
            CHECK_GITHUB_GATES=0
            ;;
        --require-do-gate)
            REQUIRE_DO_GATE=1
            ;;
        --allow-existing-tag)
            ALLOW_EXISTING_TAG=1
            ;;
        --create-tag)
            CREATE_TAG=1
            ;;
        --push-tag)
            CREATE_TAG=1
            PUSH_TAG=1
            ;;
        --manifest-dir)
            [ "$#" -ge 2 ] || fail "--manifest-dir requires a value"
            MANIFEST_DIR="$2"
            shift
            ;;
        --worktree-dir)
            [ "$#" -ge 2 ] || fail "--worktree-dir requires a value"
            WORKTREE_DIR="$2"
            shift
            ;;
        --keep-worktree)
            KEEP_WORKTREE=1
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
validate_bool RELEASE_TAG_CHECK_GITHUB_GATES "$CHECK_GITHUB_GATES"
validate_bool RELEASE_TAG_REQUIRE_DO_GATE "$REQUIRE_DO_GATE"
validate_bool RELEASE_TAG_ALLOW_EXISTING "$ALLOW_EXISTING_TAG"
validate_bool RELEASE_TAG_CREATE "$CREATE_TAG"
validate_bool RELEASE_TAG_PUSH "$PUSH_TAG"
validate_bool RELEASE_TAG_KEEP_WORKTREE "$KEEP_WORKTREE"

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

REF="$(normalize_ref "$REF")"
if ! printf '%s' "$REF" | grep -Eq '^[A-Za-z0-9._/@:-]+$'; then
    fail "release ref contains unsupported characters: $REF"
fi

SHA="$(resolve_ref_sha "$REF")" || fail "could not resolve release ref: $REF"
validate_sha "release SHA" "$SHA"

trap cleanup EXIT
prepare_worktree "$SHA"

if [ -z "$TAG" ]; then
    TAG="$(derive_tag_from_worktree)"
fi
validate_tag "$TAG"

mkdir -p "$MANIFEST_DIR"
{
    printf 'repo=%q\n' "$REPO"
    printf 'ref=%q\n' "$REF"
    printf 'sha=%s\n' "$SHA"
    printf 'tag=%q\n' "$TAG"
    printf 'check_github_gates=%s\n' "$CHECK_GITHUB_GATES"
    printf 'require_do_gate=%s\n' "$REQUIRE_DO_GATE"
    printf 'worktree=%q\n' "$WORKTREE_DIR"
} >"$MANIFEST_DIR/release-tag-gate.env"

printf 'Release tag gate: repo=%s ref=%s sha=%s tag=%s\n' "$REPO" "$REF" "$SHA" "$TAG"

check_tag_available "$TAG" "$SHA"
run_clean_worktree_checks "$SHA" "$TAG"

if [ "$CHECK_GITHUB_GATES" = "1" ]; then
    bash scripts/check-github-release-gates.sh \
        --repo "$REPO" \
        --ref "$REF" \
        --sha "$SHA"
else
    printf 'SKIP: GitHub formal gate check disabled.\n'
fi

if [ "$REQUIRE_DO_GATE" = "1" ]; then
    check_do_release_gate_artifact "$SHA"
else
    printf 'SKIP: DO release gate artifact requirement disabled.\n'
fi

create_or_push_tag "$TAG" "$SHA"

printf 'OK: release tag gate passed for %s at %s.\n' "$TAG" "$SHA"
