#!/usr/bin/env python3
"""Write and validate apps-matrix real application configuration manifests."""

from __future__ import annotations

import argparse
import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


WORDPRESS_PLUGIN_DIRS = {
    "wordpress-w3-total-cache": "w3-total-cache",
    "wordpress-litespeed-cache": "litespeed-cache",
    "wordpress-wordfence": "wordfence",
    "wordpress-redirection": "redirection",
    "wordpress-ewww": "ewww-image-optimizer",
    "wordpress-wp-optimize": "wp-optimize",
}

HEALTH_PROBES = {
    "wordpress": ("wordpress-core", "wordpress-option"),
    "laravel": ("laravel-about",),
    "drupal": ("drupal-status",),
    "nextcloud": ("nextcloud-status",),
    "joomla": ("joomla-cli-list",),
    "mediawiki": ("mediawiki-version",),
}


def expected_health_probes(scenario: str) -> tuple[str, ...]:
    probes = list(HEALTH_PROBES.get(app_name(scenario), ()))
    if scenario in WORDPRESS_PLUGIN_DIRS:
        probes.append("wordpress-plugin")
    return tuple(probes)


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def check_file(docroot: Path, relative_path: str) -> dict[str, Any]:
    path = docroot / relative_path
    return {
        "name": f"file:{relative_path}",
        "kind": "file_exists",
        "path": relative_path,
        "ok": path.is_file(),
    }


def check_dir(docroot: Path, relative_path: str) -> dict[str, Any]:
    path = docroot / relative_path
    return {
        "name": f"dir:{relative_path}",
        "kind": "dir_exists",
        "path": relative_path,
        "ok": path.is_dir(),
    }


def check_contains(docroot: Path, relative_path: str, needle: str, name: str) -> dict[str, Any]:
    path = docroot / relative_path
    return {
        "name": name,
        "kind": "file_contains",
        "path": relative_path,
        "needle": needle,
        "ok": path.is_file() and needle in read_text(path),
    }


def json_value(path: Path, keys: tuple[str, ...]) -> Any:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None

    value: Any = payload
    for key in keys:
        if not isinstance(value, dict):
            return None
        value = value.get(key)
    return value


def check_json_value(
    docroot: Path,
    relative_path: str,
    keys: tuple[str, ...],
    expected: Any,
    name: str,
) -> dict[str, Any]:
    path = docroot / relative_path
    actual = json_value(path, keys)
    return {
        "name": name,
        "kind": "json_value",
        "path": relative_path,
        "keys": list(keys),
        "expected": expected,
        "actual": actual,
        "ok": actual == expected,
    }


def check_health_probe(docroot: Path, probe: str, require_ok: bool) -> dict[str, Any]:
    relative_path = f".apps-matrix/health/{probe}.json"
    path = docroot / relative_path
    ok = path.is_file()
    if ok and require_ok:
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            ok = False
        else:
            ok = payload.get("result") == "ok"
    return {
        "name": f"health:{probe}",
        "kind": "health_probe",
        "path": relative_path,
        "require_ok": require_ok,
        "ok": ok,
    }


def expected_checks(
    scenario: str,
    docroot: Path,
    app_install_enabled: bool,
    deep_config_enabled: bool,
    strict_plugin_install: bool,
    health_probes_enabled: bool,
    enforce_health_probes: bool,
) -> list[dict[str, Any]]:
    checks: list[dict[str, Any]] = [
        check_file(docroot, ".htaccess"),
        check_file(docroot, "index.php"),
    ]
    if app_install_enabled and health_probes_enabled:
        for probe in expected_health_probes(scenario):
            checks.append(check_health_probe(docroot, probe, enforce_health_probes))

    if scenario.startswith("wordpress"):
        db_name = f"matrix_{scenario.replace('-', '_')}"
        checks.extend(
            [
                check_file(docroot, "wp-config.php"),
                check_contains(docroot, ".htaccess", "RewriteRule . /index.php", "wordpress-front-controller"),
            ]
        )
        if app_install_enabled:
            checks.extend(
                [
                    check_contains(docroot, "wp-config.php", db_name, "wordpress-db-name"),
                    check_contains(docroot, "wp-config.php", "DB_HOST", "wordpress-db-host"),
                ]
            )
        if scenario == "wordpress-redirection":
            checks.append(
                check_contains(docroot, ".htaccess", "Redirect 301 /legacy-path/", "wordpress-redirection-rules")
            )
        if scenario in {
            "wordpress-w3-total-cache",
            "wordpress-litespeed-cache",
            "wordpress-ewww",
            "wordpress-wp-optimize",
        }:
            checks.extend(
                [
                    check_contains(docroot, ".htaccess", "ExpiresActive On", "wordpress-cache-expires"),
                    check_contains(docroot, ".htaccess", "Cache-Control", "wordpress-cache-control"),
                ]
            )
        plugin_dir = WORDPRESS_PLUGIN_DIRS.get(scenario)
        if app_install_enabled and strict_plugin_install and plugin_dir:
            checks.append(check_dir(docroot, f"wp-content/plugins/{plugin_dir}"))
        if app_install_enabled and deep_config_enabled and plugin_dir:
            checks.extend(
                [
                    check_file(docroot, ".apps-matrix/wordpress_plugin_config.json"),
                    check_contains(
                        docroot,
                        ".apps-matrix/wordpress_plugin_config.json",
                        scenario,
                        "wordpress-plugin-config-snapshot",
                    ),
                ]
            )
    elif scenario == "laravel":
        checks.extend(
            [
                check_file(docroot, ".env"),
                check_contains(docroot, ".htaccess", '<Files ".env">', "laravel-env-deny"),
            ]
        )
        if app_install_enabled:
            checks.extend(
                [
                    check_file(docroot, ".apps-matrix/laravel_config.json"),
                    check_json_value(
                        docroot,
                        ".apps-matrix/laravel_config.json",
                        ("config", "DB_DATABASE"),
                        "matrix_laravel",
                        "laravel-db-name",
                    ),
                    check_json_value(
                        docroot,
                        ".apps-matrix/laravel_config.json",
                        ("config", "DB_CONNECTION"),
                        "mysql",
                        "laravel-db-connection",
                    ),
                ]
            )
    elif scenario == "drupal":
        checks.extend(
            [
                check_file(docroot, "sites/default/settings.php"),
                check_file(docroot, "core/misc/drupal.js"),
                check_contains(docroot, ".htaccess", "settings.php", "drupal-settings-deny"),
            ]
        )
        if app_install_enabled:
            checks.extend(
                [
                    check_contains(docroot, "sites/default/settings.php", "matrix_drupal", "drupal-db-name"),
                    check_contains(docroot, "sites/default/settings.php", "databases", "drupal-database-config"),
                    check_file(docroot, ".apps-matrix/drupal_semantic.json"),
                    check_json_value(
                        docroot,
                        ".apps-matrix/drupal_semantic.json",
                        ("database_name",),
                        "matrix_drupal",
                        "drupal-semantic-db-name",
                    ),
                    check_json_value(
                        docroot,
                        ".apps-matrix/drupal_semantic.json",
                        ("settings_php_present",),
                        True,
                        "drupal-semantic-settings",
                    ),
                ]
            )
    elif scenario == "nextcloud":
        checks.extend(
            [
                check_file(docroot, "config/config.php"),
                check_dir(docroot, "data"),
                check_contains(docroot, ".htaccess", "RedirectMatch 403 ^/data/", "nextcloud-data-deny"),
            ]
        )
        if app_install_enabled:
            checks.extend(
                [
                    check_contains(docroot, "config/config.php", "matrix_nextcloud", "nextcloud-db-name"),
                    check_contains(docroot, "config/config.php", "installed", "nextcloud-installed-flag"),
                    check_file(docroot, ".apps-matrix/nextcloud_semantic.json"),
                    check_json_value(
                        docroot,
                        ".apps-matrix/nextcloud_semantic.json",
                        ("database_name",),
                        "matrix_nextcloud",
                        "nextcloud-semantic-db-name",
                    ),
                    check_json_value(
                        docroot,
                        ".apps-matrix/nextcloud_semantic.json",
                        ("installed",),
                        True,
                        "nextcloud-semantic-installed",
                    ),
                ]
            )
    elif scenario == "joomla":
        checks.extend(
            [
                check_file(docroot, "configuration.php"),
                check_file(docroot, "media/system/js/core.js"),
                check_contains(docroot, ".htaccess", "configuration.php", "joomla-config-deny"),
            ]
        )
        if app_install_enabled:
            checks.extend(
                [
                    check_contains(docroot, "configuration.php", "matrix_joomla", "joomla-db-name"),
                    check_contains(docroot, "configuration.php", "public $db", "joomla-db-config"),
                    check_file(docroot, ".apps-matrix/joomla_semantic.json"),
                    check_json_value(
                        docroot,
                        ".apps-matrix/joomla_semantic.json",
                        ("database_name",),
                        "matrix_joomla",
                        "joomla-semantic-db-name",
                    ),
                    check_json_value(
                        docroot,
                        ".apps-matrix/joomla_semantic.json",
                        ("db_type",),
                        "mysqli",
                        "joomla-semantic-db-type",
                    ),
                ]
            )
    elif scenario == "mediawiki":
        checks.extend(
            [
                check_file(docroot, "resources/assets/wiki.png"),
                check_file(docroot, "maintenance/run.php"),
                check_contains(docroot, ".htaccess", "RewriteRule ^wiki/(.*)$", "mediawiki-pretty-url"),
            ]
        )
        if app_install_enabled:
            checks.extend(
                [
                    check_file(docroot, "LocalSettings.php"),
                    check_contains(docroot, "LocalSettings.php", "matrix_mediawiki", "mediawiki-db-name"),
                    check_contains(docroot, "LocalSettings.php", "$wgDBname", "mediawiki-db-config"),
                    check_file(docroot, ".apps-matrix/mediawiki_semantic.json"),
                    check_json_value(
                        docroot,
                        ".apps-matrix/mediawiki_semantic.json",
                        ("database_name",),
                        "matrix_mediawiki",
                        "mediawiki-semantic-db-name",
                    ),
                    check_json_value(
                        docroot,
                        ".apps-matrix/mediawiki_semantic.json",
                        ("site_name",),
                        "Apps Matrix",
                        "mediawiki-semantic-site-name",
                    ),
                ]
            )
    else:
        checks.append(
            {
                "name": "known-scenario",
                "kind": "scenario_known",
                "ok": False,
                "detail": f"unknown scenario: {scenario}",
            }
        )

    return checks


def app_name(scenario: str) -> str:
    if scenario.startswith("wordpress"):
        return "wordpress"
    return scenario


def write_manifest(args: argparse.Namespace) -> dict[str, Any]:
    docroot = args.docroot.resolve()
    checks = expected_checks(
        args.scenario,
        docroot,
        args.app_install_enabled,
        args.deep_config_enabled,
        args.strict_plugin_install,
        args.health_probes_enabled,
        args.enforce_health_probes,
    )
    manifest: dict[str, Any] = {
        "schema_version": 1,
        "scenario": args.scenario,
        "app": app_name(args.scenario),
        "docroot": str(docroot),
        "app_install_enabled": args.app_install_enabled,
        "deep_config_enabled": args.deep_config_enabled,
        "strict_plugin_install": args.strict_plugin_install,
        "health_probes_enabled": args.health_probes_enabled,
        "enforce_health_probes": args.enforce_health_probes,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "settings": {
            "base_url": os.environ.get("MATRIX_REAL_BASE_URL", "http://localhost"),
            "db_runtime_host": os.environ.get("MATRIX_REAL_DB_RUNTIME_HOST", ""),
            "db_runtime_port": os.environ.get("MATRIX_REAL_DB_RUNTIME_PORT", ""),
            "probe_mode": os.environ.get("MATRIX_PROBE_MODE", "enabled"),
        },
        "checks": checks,
    }
    target_dir = docroot / ".apps-matrix"
    target_dir.mkdir(parents=True, exist_ok=True)
    target = target_dir / "app_config.json"
    target.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def validate_manifest(args: argparse.Namespace) -> int:
    docroot = args.docroot.resolve()
    manifest_path = docroot / ".apps-matrix" / "app_config.json"
    if not manifest_path.exists():
        print(f"missing app config manifest: {manifest_path}", file=sys.stderr)
        return 1

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"invalid app config manifest {manifest_path}: {exc}", file=sys.stderr)
        return 1

    if not isinstance(manifest, dict):
        print(f"invalid app config manifest {manifest_path}: root must be an object", file=sys.stderr)
        return 1
    if manifest.get("scenario") != args.scenario:
        print(
            f"app config scenario mismatch: expected {args.scenario}, got {manifest.get('scenario')}",
            file=sys.stderr,
        )
        return 1
    if Path(str(manifest.get("docroot", ""))).resolve() != docroot:
        print(
            f"app config docroot mismatch: expected {docroot}, got {manifest.get('docroot')}",
            file=sys.stderr,
        )
        return 1

    expected = expected_checks(
        args.scenario,
        docroot,
        bool(manifest.get("app_install_enabled")),
        bool(manifest.get("deep_config_enabled")),
        bool(manifest.get("strict_plugin_install")),
        bool(manifest.get("health_probes_enabled")),
        bool(manifest.get("enforce_health_probes")),
    )
    failures = [check for check in expected if not check.get("ok")]
    if failures:
        for failure in failures:
            print(
                f"{args.scenario}: failed app config check {failure.get('name')} at {failure.get('path', '<none>')}",
                file=sys.stderr,
            )
        return 1

    print(f"validated app config for {args.scenario}: {manifest_path}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    for name in ("write", "validate"):
        command = subparsers.add_parser(name)
        command.add_argument("--scenario", required=True)
        command.add_argument("--docroot", required=True, type=Path)
        command.add_argument("--app-install-enabled", action="store_true")
        command.add_argument("--deep-config-enabled", action="store_true")
        command.add_argument("--strict-plugin-install", action="store_true")
        command.add_argument("--health-probes-enabled", action="store_true")
        command.add_argument("--enforce-health-probes", action="store_true")

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.command == "write":
        write_manifest(args)
        return 0
    if args.command == "validate":
        return validate_manifest(args)
    raise ValueError(f"unknown command: {args.command}")


if __name__ == "__main__":
    sys.exit(main())
