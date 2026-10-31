#!/usr/bin/env python3
"""List apps-matrix artifacts for upload or transfer."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


SUMMARY_FILES = (
    "summary.csv",
    "known_diff.csv",
    "failures.csv",
    "performance.csv",
    "full-matrix-timing.csv",
    "real-install-cache.csv",
    "real-install-steps.csv",
    "real-install-health.csv",
    "budget-suggestions.env",
    "artifact-manifest.txt",
    "scenario-selection.txt",
)

SENSITIVE_ARTIFACT_BASENAMES = {
    ".env",
    "LocalSettings.php",
    "configuration.php",
    "settings.php",
    "wp-config.php",
}

SENSITIVE_ARTIFACT_PARTS = {
    "config/config.php",
    "sites/default/settings.php",
}


def is_sensitive_artifact(path: Path) -> bool:
    if path.name in SENSITIVE_ARTIFACT_BASENAMES:
        return True
    normalized = path.as_posix()
    return any(part in normalized for part in SENSITIVE_ARTIFACT_PARTS)


def relative_files(root: Path, paths: list[Path]) -> list[str]:
    files: list[str] = []
    for path in paths:
        if path.is_file() and not is_sensitive_artifact(path.relative_to(root)):
            files.append(path.relative_to(root).as_posix())
    return files


def summary_artifacts(root: Path) -> list[str]:
    paths: list[Path] = [root / name for name in SUMMARY_FILES]
    for dirname in (
        "results",
        "artifacts",
        "logs",
        "real-install-logs",
        "real-install-plans",
        "real-install-health",
    ):
        base = root / dirname
        if base.exists():
            paths.extend(path for path in base.rglob("*") if path.is_file())

    fixtures = root / "fixtures"
    if fixtures.exists():
        paths.extend(path for path in fixtures.rglob("*") if path.is_file() and "/.apps-matrix/" in path.as_posix())
        paths.extend(path for path in fixtures.rglob("final.htaccess") if path.is_file())
        paths.extend(path for path in fixtures.rglob(".htaccess") if path.is_file())
    return sorted(set(relative_files(root, paths)))


def full_artifacts(root: Path) -> list[str]:
    return sorted(
        path.relative_to(root).as_posix()
        for path in root.rglob("*")
        if path.is_file() and not is_sensitive_artifact(path.relative_to(root))
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--mode", choices=("summary", "full"), default="summary")
    parser.add_argument(
        "--copy-to",
        type=Path,
        help="Copy listed artifacts into this directory, preserving relative paths.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.out_dir.resolve()
    if args.mode == "full":
        artifacts = full_artifacts(root)
    else:
        artifacts = summary_artifacts(root)
    if args.copy_to is not None:
        dest_root = args.copy_to.resolve()
        for artifact in artifacts:
            source = root / artifact
            dest = dest_root / artifact
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, dest)
    for artifact in artifacts:
        print(artifact)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
