#!/usr/bin/env python3
"""Read apps-matrix MANIFEST.yaml for tier and PR case selection."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def parse_manifest(path: Path) -> dict[str, dict[str, object]]:
    scenarios: dict[str, dict[str, object]] = {}
    current: dict[str, object] | None = None
    in_cases = False

    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.startswith("  - name:"):
            name = raw.split(":", 1)[1].strip()
            current = {"name": name, "cases_for_pr": []}
            scenarios[name] = current
            in_cases = False
            continue
        if current is None:
            continue
        stripped = raw.strip()
        if stripped.startswith("tier:"):
            current["tier"] = stripped.split(":", 1)[1].strip()
            in_cases = False
        elif stripped.startswith("priority:"):
            current["priority"] = stripped.split(":", 1)[1].strip()
            in_cases = False
        elif stripped == "cases_for_pr:":
            in_cases = True
        elif in_cases and stripped.startswith("- "):
            cases = current.setdefault("cases_for_pr", [])
            assert isinstance(cases, list)
            cases.append(stripped[2:].strip())
        elif raw.startswith("    ") and stripped:
            in_cases = False

    return scenarios


def should_run(entry: dict[str, object], tier: str) -> bool:
    if tier in ("all", "nightly"):
        return True
    if tier == "pr":
        return entry.get("tier") == "pr"
    raise ValueError(f"unknown tier: {tier}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--tier", choices=("pr", "nightly", "all"), required=True)
    parser.add_argument("--should-run", action="store_true")
    parser.add_argument("--cases", action="store_true")
    return parser.parse_args()


def main() -> int:
    try:
        args = parse_args()
        scenarios = parse_manifest(args.manifest)
        entry = scenarios.get(args.scenario)
        if entry is None:
            return 2
        runs = should_run(entry, args.tier)
        if args.should_run:
            return 0 if runs else 2
        if args.cases and runs and args.tier == "pr":
            print(",".join(str(case) for case in entry.get("cases_for_pr", [])))
        elif args.cases:
            print("")
        return 0 if runs else 2
    except (OSError, ValueError) as exc:
        print(f"apps-matrix manifest: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
