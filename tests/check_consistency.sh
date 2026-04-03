#!/bin/bash
# =============================================================================
# Directive Support Consistency Checker
#
# Scans enum, parser, printer, executor, and generators to report
# inconsistencies like:
#   - enum has type, but parser doesn't recognize it
#   - parser handles type, but printer doesn't output it
#   - generator claims coverage but silently falls back
#
# Usage: bash tests/check_consistency.sh
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PASS=0
WARN=0
ERRORS=""

check() {
    local label="$1" found="$2"
    if [ "$found" = "yes" ]; then
        PASS=$((PASS + 1))
    else
        WARN=$((WARN + 1))
        ERRORS="$ERRORS\n  - $label"
    fi
}

echo "========================================"
echo " Directive Support Consistency Check"
echo "========================================"
echo ""

# --- Step 1: Extract all DIR_ enum values from htaccess_directive.h ---
ENUM_FILE="$ROOT/include/htaccess_directive.h"
ENUM_TYPES=$(grep -oP 'DIR_[A-Z_]+' "$ENUM_FILE" | grep -v "DIR_COUNT\|DIR_NONE" | sort -u)
ENUM_COUNT=$(echo "$ENUM_TYPES" | wc -l)
echo "Enum types found: $ENUM_COUNT"

# --- Step 2: Check parser coverage ---
PARSER_FILE="$ROOT/src/htaccess_parser.c"
echo ""
echo "--- Parser coverage ---"
for t in $ENUM_TYPES; do
    found="no"
    grep -q "$t" "$PARSER_FILE" 2>/dev/null && found="yes"
    check "Parser missing: $t" "$found"
done
echo "  Parser: $PASS covered, $WARN missing"
P_PASS=$PASS; P_WARN=$WARN
PASS=0; WARN=0

# --- Step 3: Check printer coverage ---
PRINTER_FILE="$ROOT/src/htaccess_printer.c"
echo ""
echo "--- Printer coverage ---"
for t in $ENUM_TYPES; do
    found="no"
    grep -q "$t" "$PRINTER_FILE" 2>/dev/null && found="yes"
    check "Printer missing: $t" "$found"
done
echo "  Printer: $PASS covered, $WARN missing"
PR_PASS=$PASS; PR_WARN=$WARN
PASS=0; WARN=0

# --- Step 4: Check executor coverage ---
echo ""
echo "--- Executor coverage (mod_htaccess.c + exec_*.c) ---"
EXEC_FILES=$(find "$ROOT/src" -name "htaccess_exec_*.c" -o -name "mod_htaccess.c")
for t in $ENUM_TYPES; do
    found="no"
    for f in $EXEC_FILES; do
        grep -q "$t" "$f" 2>/dev/null && found="yes" && break
    done
    check "Executor missing: $t" "$found"
done
echo "  Executors: $PASS covered, $WARN missing"
E_PASS=$PASS; E_WARN=$WARN
PASS=0; WARN=0

# --- Step 5: Check dirwalker deep-copy coverage ---
WALKER_FILE="$ROOT/src/htaccess_dirwalker.c"
echo ""
echo "--- Dirwalker deep-copy ---"
# These types have heap fields that need deep-copy
NEED_COPY="DIR_REDIRECT DIR_REDIRECT_MATCH DIR_FILES_MATCH DIR_FILES DIR_IFMODULE DIR_REQUIRE_ANY_OPEN DIR_REQUIRE_ALL_OPEN DIR_LIMIT DIR_LIMIT_EXCEPT DIR_SETENVIF DIR_SETENVIF_NOCASE DIR_BROWSER_MATCH DIR_HEADER_EDIT DIR_HEADER_EDIT_STAR DIR_HEADER_ALWAYS_EDIT DIR_HEADER_ALWAYS_EDIT_STAR"
for t in $NEED_COPY; do
    found="no"
    grep -q "$t" "$WALKER_FILE" 2>/dev/null && found="yes"
    check "Dirwalker missing deep-copy: $t" "$found"
done
echo "  Dirwalker: $PASS covered, $WARN missing"
D_PASS=$PASS; D_WARN=$WARN
PASS=0; WARN=0

# --- Step 6: Check directive free coverage ---
FREE_FILE="$ROOT/src/htaccess_directive.c"
echo ""
echo "--- Directive free coverage ---"
NEED_FREE="DIR_REDIRECT DIR_REDIRECT_MATCH DIR_FILES_MATCH DIR_SETENVIF DIR_SETENVIF_NOCASE DIR_BROWSER_MATCH DIR_IFMODULE DIR_FILES DIR_REQUIRE_ANY_OPEN DIR_REQUIRE_ALL_OPEN DIR_LIMIT DIR_LIMIT_EXCEPT DIR_HEADER_EDIT DIR_HEADER_EDIT_STAR DIR_HEADER_ALWAYS_EDIT DIR_HEADER_ALWAYS_EDIT_STAR"
for t in $NEED_FREE; do
    found="no"
    grep -q "$t" "$FREE_FILE" 2>/dev/null && found="yes"
    check "Free missing: $t" "$found"
done
echo "  Free: $PASS covered, $WARN missing"
F_PASS=$PASS; F_WARN=$WARN
PASS=0; WARN=0

# --- Step 7: Generator coverage ---
GEN_FILE="$ROOT/tests/generators/gen_directive.h"
echo ""
echo "--- Generator coverage ---"
for t in $ENUM_TYPES; do
    found="no"
    grep -q "$t" "$GEN_FILE" 2>/dev/null && found="yes"
    check "Generator missing: $t" "$found"
done
echo "  Generator: $PASS covered, $WARN missing"
G_PASS=$PASS; G_WARN=$WARN

# --- Summary ---
TOTAL_WARN=$((P_WARN + PR_WARN + E_WARN + D_WARN + F_WARN + G_WARN))
echo ""
echo "========================================"
echo " Summary"
echo "========================================"
echo "  Enum types:     $ENUM_COUNT"
echo "  Parser gaps:    $P_WARN"
echo "  Printer gaps:   $PR_WARN"
echo "  Executor gaps:  $E_WARN"
echo "  Dirwalker gaps: $D_WARN"
echo "  Free gaps:      $F_WARN"
echo "  Generator gaps: $G_WARN"
echo "  Total gaps:     $TOTAL_WARN"

if [ "$TOTAL_WARN" -gt 0 ]; then
    echo ""
    echo "  Gaps found:$ERRORS"
fi
echo "========================================"

exit 0
