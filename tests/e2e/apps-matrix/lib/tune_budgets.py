#!/usr/bin/env python3
"""Suggest apps-matrix budget env values from timing CSV artifacts."""

from __future__ import annotations

import argparse
import csv
import math
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


SCENARIO_GROUPS = {
    "wordpress": [
        "wordpress-core",
        "wordpress-redirection",
        "wordpress-w3-total-cache",
        "wordpress-litespeed-cache",
        "wordpress-wordfence",
        "wordpress-ewww",
        "wordpress-wp-optimize",
    ],
    "cms": ["drupal", "nextcloud", "joomla", "mediawiki"],
    "framework": ["laravel"],
}
SCENARIO_GROUPS["heavy"] = (
    SCENARIO_GROUPS["wordpress"] + SCENARIO_GROUPS["framework"] + SCENARIO_GROUPS["cms"]
)
SCENARIO_GROUPS["all"] = SCENARIO_GROUPS["heavy"]


def env_name(scenario: str, phase: str) -> str:
    normalized = scenario.upper().replace("-", "_")
    return f"MATRIX_BUDGET_{normalized}_{phase.upper()}_MS"


def percentile(values: list[int], percent: float) -> int:
    if not values:
        return 0
    values = sorted(values)
    index = min(len(values) - 1, max(0, math.ceil((percent / 100) * len(values)) - 1))
    return values[index]


def rounded_budget(value: int, multiplier: float, floor_ms: int, step_ms: int) -> int:
    raw = max(floor_ms, int(math.ceil(value * multiplier)))
    return int(math.ceil(raw / step_ms) * step_ms)


def timing_rows(out_dir: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in sorted(out_dir.rglob("full-matrix-timing.csv")):
        with path.open(encoding="utf-8", newline="") as handle:
            rows.extend(csv.DictReader(handle))
    return rows


def scenario_phase_durations(out_dir: Path) -> dict[tuple[str, str], list[int]]:
    scenario_phase_ms: dict[tuple[str, str], list[int]] = defaultdict(list)
    for row in timing_rows(out_dir):
        scenario = row.get("scenario", "").strip()
        phase = row.get("phase", "").strip()
        duration = row.get("duration_ms", "").strip()
        result = row.get("result", "").strip()
        if not scenario or not phase or not duration.isdigit() or result != "ok":
            continue
        scenario_phase_ms[(scenario, phase)].append(int(duration))
    return scenario_phase_ms


def performance_rows(out_dir: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in sorted(out_dir.rglob("performance.csv")):
        with path.open(encoding="utf-8", newline="") as handle:
            rows.extend(csv.DictReader(handle))
    return rows


@dataclass(frozen=True)
class BudgetSuggestion:
    name: str
    value_ms: int
    sample_count: int

    def env_line(self) -> str:
        return f'{self.name}="${{{self.name}:-{self.value_ms}}}"'


def scenario_budget_suggestions(args: argparse.Namespace) -> list[BudgetSuggestion]:
    scenario_phase_ms = scenario_phase_durations(args.out_dir)

    suggestions: list[BudgetSuggestion] = []
    for (scenario, phase), values in sorted(scenario_phase_ms.items()):
        if len(values) < args.min_samples:
            continue
        baseline = percentile(values, args.percentile)
        budget = rounded_budget(baseline, args.multiplier, args.floor_ms, args.step_ms)
        suggestions.append(BudgetSuggestion(env_name(scenario, phase), budget, len(values)))
    return suggestions


def per_engine_budget_suggestion(args: argparse.Namespace) -> BudgetSuggestion | None:
    values: list[int] = []
    for row in performance_rows(args.out_dir):
        duration = row.get("total_duration_ms", "").strip()
        if duration.isdigit():
            values.append(int(duration))
    if len(values) < args.min_samples:
        return None
    baseline = percentile(values, args.percentile)
    budget = rounded_budget(baseline, args.multiplier, 5000, 5000)
    return BudgetSuggestion("MATRIX_PER_ENGINE_BUDGET_MS", budget, len(values))


def render_suggestions(suggestions: list[BudgetSuggestion]) -> None:
    if not suggestions:
        return
    for suggestion in suggestions:
        print(suggestion.env_line())


def expand_scenarios(value: str) -> list[str]:
    scenarios: list[str] = []
    seen: set[str] = set()
    for token in value.replace(" ", ",").split(","):
        token = token.strip()
        if not token:
            continue
        expanded = SCENARIO_GROUPS.get(token, [token])
        for scenario in expanded:
            if scenario not in seen:
                seen.add(scenario)
                scenarios.append(scenario)
    return scenarios


def expand_phases(value: str) -> list[str]:
    phases: list[str] = []
    seen: set[str] = set()
    for token in value.replace(" ", ",").split(","):
        token = token.strip()
        if not token:
            continue
        if token not in {"install", "verify", "all"}:
            raise ValueError(f"unsupported phase in --require-phases: {token}")
        if token not in seen:
            seen.add(token)
            phases.append(token)
    return phases


def missing_required_samples(args: argparse.Namespace) -> list[str]:
    if not args.require_scenarios:
        return []

    scenarios = expand_scenarios(args.require_scenarios)
    phases = expand_phases(args.require_phases)
    durations = scenario_phase_durations(args.out_dir)

    missing: list[str] = []
    for scenario in scenarios:
        for phase in phases:
            count = len(durations.get((scenario, phase), []))
            if count < args.min_samples:
                missing.append(
                    f"{scenario}:{phase} has {count} ok sample(s), "
                    f"requires {args.min_samples}"
                )
    return missing


def update_env_file(path: Path, suggestions: list[BudgetSuggestion]) -> None:
    if not suggestions:
        return

    path.parent.mkdir(parents=True, exist_ok=True)
    existing = path.read_text(encoding="utf-8") if path.exists() else ""
    lines = existing.splitlines()
    suggestion_by_name = {suggestion.name: suggestion for suggestion in suggestions}
    seen: set[str] = set()
    pattern = re.compile(r"^([A-Z0-9_]+)=")

    updated: list[str] = []
    for line in lines:
        match = pattern.match(line)
        if match and match.group(1) in suggestion_by_name:
            name = match.group(1)
            updated.append(suggestion_by_name[name].env_line())
            seen.add(name)
        else:
            updated.append(line)

    missing = [suggestion for suggestion in suggestions if suggestion.name not in seen]
    if missing:
        if updated and updated[-1].strip():
            updated.append("")
        updated.append("# Tuned from apps-matrix timing artifacts.")
        updated.extend(suggestion.env_line() for suggestion in missing)

    path.write_text("\n".join(updated) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--percentile", default=95.0, type=float)
    parser.add_argument("--multiplier", default=1.35, type=float)
    parser.add_argument("--floor-ms", default=30000, type=int)
    parser.add_argument("--step-ms", default=30000, type=int)
    parser.add_argument(
        "--min-samples",
        default=1,
        type=int,
        help="Minimum timing samples required before emitting/updating a budget.",
    )
    parser.add_argument(
        "--include-per-engine",
        action="store_true",
        help="Also emit a single MATRIX_PER_ENGINE_BUDGET_MS suggestion.",
    )
    parser.add_argument(
        "--update-env",
        type=Path,
        help="Rewrite the given budgets.env file with emitted suggestions.",
    )
    parser.add_argument(
        "--require-scenarios",
        default="",
        help=(
            "Comma-separated scenarios or groups that must have enough ok samples "
            "before suggestions are emitted. Groups: all, heavy, wordpress, cms, framework."
        ),
    )
    parser.add_argument(
        "--require-phases",
        default="all",
        help="Comma-separated phases required with --require-scenarios. Default: all.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.min_samples < 1:
        raise ValueError("--min-samples must be at least 1")

    missing = missing_required_samples(args)
    if missing:
        for item in missing:
            print(f"# insufficient samples: {item}")
        return 2

    suggestions = scenario_budget_suggestions(args)
    if not suggestions:
        print(f"# no full-matrix-timing.csv rows found under {args.out_dir}")

    if args.include_per_engine:
        per_engine = per_engine_budget_suggestion(args)
        if per_engine is not None:
            suggestions.append(per_engine)

    render_suggestions(suggestions)
    if args.update_env is not None:
        update_env_file(args.update_env, suggestions)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
