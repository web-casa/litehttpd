#!/usr/bin/env python3
"""Validate generated apps-matrix per-case JSON result files."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from run_cases import validate_result_payload


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", required=True, type=Path)
    parser.add_argument("--schema", type=Path, help="Accepted for CI readability.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.results_dir.exists():
        print(f"missing results directory: {args.results_dir}", file=sys.stderr)
        return 1

    failures = []
    files = sorted(args.results_dir.rglob("*.json"))
    if not files:
        print(f"no result JSON files under {args.results_dir}", file=sys.stderr)
        return 1

    for path in files:
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
            validate_result_payload(payload)
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            failures.append(f"{path}: {exc}")

    if failures:
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1

    print(f"validated {len(files)} apps-matrix result file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
