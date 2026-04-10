#!/bin/bash
# =============================================================================
# Directive Coverage Analysis Tool for OLS .htaccess Module
#
# Extracts directive types from .htaccess files and compares them against
# the module's 59 supported directive types. Outputs a coverage report
# with supported/unsupported counts and percentage.
#
# Unsupported directives are marked as WARNING (do not block CI).
#
# Requirements: 20.1, 20.2, 20.3, 20.4
# =============================================================================

# ---------------------------------------------------------------------------
# Supported directives — all 59 types the module handles
#
# Mapped from directive_type_t enum in include/htaccess_directive.h:
#   v1 (0-27): 28 types
#   v2 (28-58): 31 types
#   Total: 59
#
# Note: Some enum values map to the same .htaccess keyword (e.g.,
# DIR_HEADER_SET / DIR_HEADER_ALWAYS_SET both use "Header";
# DIR_ALLOW_FROM uses "Allow"; DIR_DENY_FROM uses "Deny").
# We list the unique directive *names* as they appear in .htaccess files.
# ---------------------------------------------------------------------------
SUPPORTED_DIRECTIVES=(
    # v1 — Header family (DIR_HEADER_SET..DIR_HEADER_ADD, DIR_REQUEST_HEADER_*)
    "Header"
    "RequestHeader"

    # v1 — PHP directives
    "php_value"
    "php_flag"
    "php_admin_value"
    "php_admin_flag"

    # v1 — Access control
    "Order"
    "Allow"
    "Deny"

    # v1 — Redirect
    "Redirect"
    "RedirectMatch"

    # v1 — Error handling
    "ErrorDocument"

    # v1 — Container / Expires / Env
    "FilesMatch"
    "ExpiresActive"
    "ExpiresByType"
    "SetEnv"
    "SetEnvIf"
    "SetEnvIfNoCase"
    "BrowserMatch"

    # v1 — Brute force protection
    "BruteForceProtection"
    "BruteForceAllowedAttempts"
    "BruteForceWindow"
    "BruteForceAction"
    "BruteForceThrottleDuration"

    # v2 — Panel core
    "IfModule"
    "Options"
    "Files"

    # v2 — Expires default
    "ExpiresDefault"

    # v2 — Require family
    "Require"
    "RequireAny"
    "RequireAll"

    # v2 — Limit containers
    "Limit"
    "LimitExcept"

    # v2 — Auth directives
    "AuthType"
    "AuthName"
    "AuthUserFile"

    # v2 — Handler / Type directives
    "AddHandler"
    "SetHandler"
    "AddType"
    "DirectoryIndex"
    "ForceType"
    "AddEncoding"
    "AddCharset"

    # v2 — Brute force enhancements
    "BruteForceXForwardedFor"
    "BruteForceWhitelist"
    "BruteForceProtectPath"
)

# ---------------------------------------------------------------------------
# extract_directives — Parse .htaccess files and output unique directive names
#
# Reads one or more .htaccess files (paths passed as arguments) and extracts
# the directive name from each non-comment, non-empty line.
#
# Handles:
#   - Standalone directives: first word of the line (e.g., "Header", "Options")
#   - Container opening tags: <IfModule ...> → "IfModule"
#   - Closing tags (</...>) and blank lines are skipped
#   - Comment lines (starting with #) are skipped
#
# Output: sorted unique list of directive names, one per line (to stdout)
#
# Usage: extract_directives file1.htaccess [file2.htaccess ...]
# ---------------------------------------------------------------------------
extract_directives() {
    if [[ $# -eq 0 ]]; then
        echo "Usage: extract_directives <htaccess_file> [...]" >&2
        return 1
    fi

    local directives=()

    for file in "$@"; do
        if [[ ! -f "$file" ]]; then
            echo "WARNING: File not found: ${file}" >&2
            continue
        fi

        while IFS= read -r line || [[ -n "$line" ]]; do
            # Trim leading whitespace
            line="${line#"${line%%[![:space:]]*}"}"

            # Skip empty lines
            [[ -z "$line" ]] && continue

            # Skip comments
            [[ "$line" == \#* ]] && continue

            # Skip closing tags (</...>)
            [[ "$line" == \</* ]] && continue

            local directive_name=""

            if [[ "$line" == \<* ]]; then
                # Container opening tag: extract name between < and first space or >
                # e.g., "<IfModule mod_headers.c>" → "IfModule"
                # e.g., "<Files .htaccess>" → "Files"
                directive_name=$(echo "$line" | sed 's/^<\s*//' | sed 's/[[:space:]>].*//')
                # Remove leading ! for negated conditions like <!IfModule>
                directive_name="${directive_name#!}"
            else
                # Standalone directive: first word
                directive_name=$(echo "$line" | awk '{print $1}')
            fi

            if [[ -n "$directive_name" ]]; then
                directives+=("$directive_name")
            fi
        done < "$file"
    done

    # Output sorted unique list
    printf '%s\n' "${directives[@]}" | sort -u
}

# ---------------------------------------------------------------------------
# check_coverage — Compare directive list against supported directives
#
# Accepts directive names as arguments (one per argument) and compares
# against SUPPORTED_DIRECTIVES. Sets global result variables.
#
# Sets global variables:
#   _COV_TOTAL       — total unique directives found
#   _COV_SUPPORTED   — count of supported directives
#   _COV_UNSUPPORTED — count of unsupported directives
#   _COV_UNSUPPORTED_LIST — newline-separated list of unsupported names
#   _COV_SUPPORTED_LIST   — newline-separated list of supported names
#
# Usage: check_coverage $(extract_directives file.htaccess)
#   or:  check_coverage "Header" "Options" "RewriteRule"
# ---------------------------------------------------------------------------
check_coverage() {
    _COV_TOTAL=0
    _COV_SUPPORTED=0
    _COV_UNSUPPORTED=0
    _COV_UNSUPPORTED_LIST=""
    _COV_SUPPORTED_LIST=""

    # Build a lookup set from SUPPORTED_DIRECTIVES
    declare -A supported_set
    for d in "${SUPPORTED_DIRECTIVES[@]}"; do
        supported_set["$d"]=1
    done

    for directive in "$@"; do
        # Trim whitespace
        directive="${directive#"${directive%%[![:space:]]*}"}"
        directive="${directive%"${directive##*[![:space:]]}"}"
        [[ -z "$directive" ]] && continue

        _COV_TOTAL=$((_COV_TOTAL + 1))

        if [[ -n "${supported_set[$directive]+_}" ]]; then
            _COV_SUPPORTED=$((_COV_SUPPORTED + 1))
            if [[ -n "$_COV_SUPPORTED_LIST" ]]; then
                _COV_SUPPORTED_LIST="${_COV_SUPPORTED_LIST}"$'\n'"${directive}"
            else
                _COV_SUPPORTED_LIST="${directive}"
            fi
        else
            _COV_UNSUPPORTED=$((_COV_UNSUPPORTED + 1))
            if [[ -n "$_COV_UNSUPPORTED_LIST" ]]; then
                _COV_UNSUPPORTED_LIST="${_COV_UNSUPPORTED_LIST}"$'\n'"${directive}"
            else
                _COV_UNSUPPORTED_LIST="${directive}"
            fi
        fi
    done

    return 0
}

# ---------------------------------------------------------------------------
# print_coverage_report — Output formatted coverage statistics
#
# Prints a report with supported/unsupported/total counts and percentage.
# Unsupported directives are marked as WARNING (Req 20.3).
#
# Parameters:
#   $1 — app name (e.g., "WordPress", "Nextcloud")
#
# Expects check_coverage() to have been called first (reads _COV_* globals).
#
# Usage: print_coverage_report "WordPress"
# ---------------------------------------------------------------------------
print_coverage_report() {
    local app_name="${1:-unknown}"

    # Calculate percentage (avoid division by zero)
    local pct=0
    if [[ "$_COV_TOTAL" -gt 0 ]]; then
        pct=$(awk "BEGIN { printf \"%.1f\", ($_COV_SUPPORTED / $_COV_TOTAL) * 100 }")
    fi

    echo ""
    echo "========================================"
    echo " Directive Coverage Report: ${app_name}"
    echo "========================================"
    echo " Total directives found:  ${_COV_TOTAL}"
    echo " Supported:               ${_COV_SUPPORTED}"
    echo " Unsupported:             ${_COV_UNSUPPORTED}"
    echo " Coverage:                ${pct}%"
    echo "----------------------------------------"

    if [[ "$_COV_UNSUPPORTED" -gt 0 ]]; then
        echo " WARNING: Unsupported directives:"
        while IFS= read -r d; do
            echo "   - ${d}"
        done <<< "$_COV_UNSUPPORTED_LIST"
    else
        echo " All directives are supported."
    fi

    echo "========================================"
    echo ""
}
