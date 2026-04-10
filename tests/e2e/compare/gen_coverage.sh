#!/bin/bash
# =============================================================================
# Generate coverage summary from compare YAML test cases
#
# Reads p0_cases.yaml, p1_cases.yaml, p2_cases.yaml and outputs a
# machine-generated coverage table. Replaces manual TEST_COVERAGE.md maintenance.
#
# Usage: bash tests/e2e/compare/gen_coverage.sh > tests/e2e/compare/COVERAGE_AUTO.md
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "# Auto-Generated Test Coverage"
echo ""
echo "> Generated from YAML case files. Do not edit manually."
echo "> Run: \`bash tests/e2e/compare/gen_coverage.sh > tests/e2e/compare/COVERAGE_AUTO.md\`"
echo ""

total=0
for yaml in "$SCRIPT_DIR"/cases/*.yaml; do
    [ -f "$yaml" ] || continue
    basename=$(basename "$yaml" .yaml)
    echo "## $basename"
    echo ""

    # Count cases
    count=$(grep -c "^- id:" "$yaml" 2>/dev/null || echo 0)
    total=$((total + count))
    echo "Total cases: $count"
    echo ""

    # Group by group field
    echo "| Group | Count | Modes |"
    echo "|-------|:-----:|-------|"
    grep "group:\|compare_mode:" "$yaml" | paste - - | \
        sed 's/.*group: *//; s/  *compare_mode: */ | /' | \
        sort | uniq -c | sort -rn | \
        while read cnt group mode; do
            echo "| $group | $cnt | $mode |"
        done
    echo ""

    # List assertion types used
    echo "Assertion types used:"
    grep -oP '^\s+\K(status|status_in|header:|body_contains|body_not_contains|probe:)' "$yaml" 2>/dev/null | sort | uniq -c | sort -rn | \
        while read cnt atype; do
            echo "  - $atype ($cnt)"
        done
    echo ""
done

echo "---"
echo ""
echo "**Total cases across all files: $total**"
