#!/usr/bin/env python3
"""Provision deterministic apps-matrix fixtures and probes."""

from __future__ import annotations

import argparse
import base64
import json
import os
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlencode, urlparse

from run_cases import ENGINES, header_value, load_cases, split_name_value


DEFAULT_FIXTURE_ROOT = "fixtures/docroots"
TTL_DEFAULT = 31536000
PROBE_FILES = ("env.php", "headers.php", "router.php", "server.php")
PROBE_MARKER = "APPS_MATRIX_V1"
EMPTY_GENERATED_INDEX_MARKER = "base64_decode('')"


def selected_engines() -> tuple[str, ...]:
    raw = os.environ.get("MATRIX_ENGINES", ",".join(ENGINES))
    engines = tuple(engine.strip() for engine in raw.split(",") if engine.strip())
    unknown = sorted(set(engines) - set(ENGINES))
    if unknown:
        raise ValueError(f"unknown MATRIX_ENGINES value(s): {', '.join(unknown)}")
    return engines


def docroot_for(engine: str, scenario: str, fixture_root: Path) -> tuple[Path, bool]:
    env_name = f"MATRIX_{engine.upper()}_DOCROOT"
    if os.environ.get(env_name):
        return Path(os.environ[env_name]).resolve(), True
    if scenario == "laravel":
        return (fixture_root / engine / scenario / "public").resolve(), False
    if scenario == "drupal":
        return (fixture_root / engine / scenario / "web").resolve(), False
    return (fixture_root / engine / scenario).resolve(), False


def clean_generated_docroot(path: Path, is_custom_docroot: bool) -> None:
    if is_custom_docroot:
        if os.environ.get("MATRIX_FIXTURE_CLEAN", "0") != "1":
            return
        marker = path / ".apps-matrix" / "managed"
        if not marker.exists():
            raise ValueError(
                f"refusing to clean custom docroot without managed marker: {path}"
            )
    if path.exists():
        shutil.rmtree(path)


def mkdir_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def write_text(path: Path, content: str) -> None:
    mkdir_parent(path)
    path.write_text(content, encoding="utf-8")


def write_binary(path: Path, content: bytes) -> None:
    mkdir_parent(path)
    path.write_bytes(content)


def path_without_query(raw_path: str) -> str:
    parsed = urlparse(raw_path)
    return parsed.path or "/"


def file_path_for_url(docroot: Path, raw_path: str) -> Path:
    path = path_without_query(raw_path).lstrip("/")
    if not path:
        return docroot / "index.php"
    if path.endswith("/"):
        return docroot / path / ".apps-matrix-keep"
    return docroot / path


def content_type_for(path: str) -> str:
    lowered = path_without_query(path).lower()
    if lowered.endswith(".css"):
        return "text/css"
    if lowered.endswith(".js"):
        return "application/javascript"
    if lowered.endswith(".jpg") or lowered.endswith(".jpeg"):
        return "image/jpeg"
    if lowered.endswith(".png"):
        return "image/png"
    if "/_probe/" in lowered:
        return "application/json"
    if lowered.endswith(".txt"):
        return "text/plain"
    return "text/html; charset=utf-8"


def assertion_value(case: dict[str, Any], assertion_name: str) -> Any | None:
    for assertion in case.get("assert", []):
        if assertion_name in assertion:
            return assertion[assertion_name]
    return None


def ttl_for_case(case: dict[str, Any]) -> int | None:
    value = assertion_value(case, "ttl_close")
    return int(value) if value is not None else None


def expected_status_for_case(case: dict[str, Any]) -> int | None:
    value = assertion_value(case, "status_exact")
    return int(value) if value is not None else None


def route_for_case(scenario: str, app: str, case: dict[str, Any]) -> dict[str, Any]:
    status = 200
    headers: dict[str, str] = {}
    body_parts = [
        f"apps-matrix scenario={scenario}",
        f"case={case['id']}",
        f"app={app}",
    ]

    if "wordpress" in app or scenario.startswith("wordpress"):
        body_parts.append("wordpress")

    for assertion in case.get("assert", []):
        name, expected = next(iter(assertion.items()))
        if name == "status_exact":
            status = int(expected)
        elif name == "location_exact":
            headers["Location"] = str(expected)
        elif name == "header_exact":
            header, expected_value = split_name_value(expected)
            headers[header] = expected_value
        elif name == "header_contains":
            header, expected_value = split_name_value(expected)
            current = header_value(headers, header)
            if expected_value.lower().startswith("max-age="):
                value = f"public, max-age={ttl_for_case(case) or TTL_DEFAULT}"
            elif expected_value == "":
                value = default_header_value(header)
            else:
                value = expected_value
            headers[header] = f"{current}, {value}" if current else value
        elif name == "header_absent":
            headers.pop(str(expected), None)
        elif name == "body_contains":
            body_parts.append(str(expected))

    if ttl_for_case(case) is not None and header_value(headers, "Cache-Control") is None:
        headers["Cache-Control"] = f"public, max-age={ttl_for_case(case)}"
    if header_value(headers, "Content-Type") is None:
        headers["Content-Type"] = content_type_for(str(case["path"]))

    if status in (301, 302, 303, 307, 308):
        body = ""
    elif "/_probe/" in str(case["path"]):
        body = json.dumps(default_probe_payload(str(case["path"])), sort_keys=True)
        headers["Content-Type"] = "application/json"
    else:
        body = "\n".join(body_parts) + "\n"

    return {"status": status, "headers": headers, "body": body}


def default_header_value(header: str) -> str:
    lowered = header.lower()
    if lowered == "vary":
        return "Accept-Encoding"
    if lowered == "cache-control":
        return f"public, max-age={TTL_DEFAULT}"
    if lowered == "content-type":
        return "text/plain"
    return "apps-matrix"


def default_probe_payload(raw_path: str) -> dict[str, Any]:
    parsed = urlparse(raw_path)
    query = parse_qs(parsed.query)
    uri = query.get("uri", [parsed.path])[0]
    return {
        "REQUEST_METHOD": "GET",
        "REQUEST_URI": uri,
        "SCRIPT_NAME": "/index.php",
        "QUERY_STRING": parsed.query,
        "PROBE_MARKER": "APPS_MATRIX_V1",
    }


def probe_route_for_case(case: dict[str, Any]) -> dict[str, Any] | None:
    probe_path = case.get("probe_path")
    if not probe_path:
        return None
    payload = default_probe_payload(str(probe_path))
    for assertion in case.get("assert", []):
        name, expected = next(iter(assertion.items()))
        if name in ("probe_field_exact", "probe_field_contains"):
            field, expected_value = split_name_value(expected)
            payload[field] = expected_value
    return {
        "status": 200,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps(payload, sort_keys=True) + "\n",
    }


def app_route_probe_path(probe_path: str) -> str | None:
    parsed = urlparse(probe_path)
    if parsed.path != "/_probe/router.php":
        return None

    target_uri = parse_qs(parsed.query).get("uri", [""])[0]
    if not target_uri.startswith("/"):
        return None

    separator = "&" if "?" in target_uri else "?"
    return f"{target_uri}{separator}{urlencode({'__apps_matrix_probe': 'router'})}"


def index_php(body: str) -> str:
    encoded_body = base64.b64encode(body.encode("utf-8")).decode("ascii")
    return f"""<?php
if (($_GET['__apps_matrix_probe'] ?? '') === 'router') {{
    header('Content-Type: application/json; charset=utf-8');
    header('X-Probe-Marker: APPS_MATRIX_V1');
    echo json_encode(
        [
            'PROBE_MARKER'    => 'APPS_MATRIX_V1',
            'REQUEST_URI'     => $_SERVER['REQUEST_URI'] ?? null,
            'SCRIPT_NAME'     => '/index.php',
            'SCRIPT_FILENAME' => rtrim($_SERVER['DOCUMENT_ROOT'] ?? '', '/') . '/index.php',
            'PHP_SELF'        => '/index.php',
            'PATH_INFO'       => $_SERVER['PATH_INFO'] ?? null,
            'REDIRECT_URL'    => $_SERVER['REDIRECT_URL'] ?? null,
            'REDIRECT_STATUS' => $_SERVER['REDIRECT_STATUS'] ?? null,
        ],
        JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES
    );
    return;
}}

header('Content-Type: text/html; charset=utf-8');
echo base64_decode('{encoded_body}');
"""


def should_replace_generated_index(path: Path) -> bool:
    if not path.exists():
        return True
    return EMPTY_GENERATED_INDEX_MARKER in path.read_text(encoding="utf-8", errors="replace")


def ensure_front_controller_body(docroot: Path, body: str) -> None:
    if not body:
        return
    target = docroot / "index.php"
    if should_replace_generated_index(target):
        write_text(target, index_php(body))


def write_case_file(docroot: Path, case: dict[str, Any], route: dict[str, Any]) -> None:
    target = file_path_for_url(docroot, str(case["path"]))
    if has_file_parent(target, docroot):
        return
    raw_path = path_without_query(str(case["path"]))
    if raw_path == "/":
        write_text(target, index_php(str(route["body"])))
        write_text(docroot / "index.html", str(route["body"]))
        return
    if raw_path.endswith("/"):
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("apps-matrix directory fixture\n", encoding="utf-8")
        return
    if raw_path.lower().endswith((".jpg", ".jpeg", ".png")):
        write_binary(target, b"apps-matrix image fixture\n")
    else:
        write_text(target, str(route["body"] or "apps-matrix fixture\n"))


def write_text_if_missing(path: Path, content: str) -> None:
    if path.exists():
        return
    write_text(path, content)


def write_binary_if_missing(path: Path, content: bytes) -> None:
    if path.exists():
        return
    write_binary(path, content)


def has_file_parent(path: Path, docroot: Path) -> bool:
    for parent in path.parents:
        if parent == docroot or parent == parent.parent:
            return False
        if parent.exists() and not parent.is_dir():
            return True
    return False


def merge_route(existing: dict[str, Any], new: dict[str, Any]) -> dict[str, Any]:
    merged = dict(existing)
    if int(existing.get("status", 200)) == int(new.get("status", 200)):
        merged["status"] = existing.get("status", new.get("status", 200))
    else:
        merged["status"] = new.get("status", existing.get("status", 200))

    headers = dict(existing.get("headers", {}))
    headers.update(dict(new.get("headers", {})))
    merged["headers"] = headers

    existing_body = str(existing.get("body", ""))
    new_body = str(new.get("body", ""))
    if new_body and new_body not in existing_body:
        merged["body"] = existing_body + new_body
    else:
        merged["body"] = existing_body or new_body
    return merged


def build_routes(
    scenario: str,
    app: str,
    cases: list[dict[str, Any]],
) -> dict[str, Any]:
    routes: dict[str, Any] = {}
    for case in cases:
        route = route_for_case(scenario, app, case)
        route_key = f"{case['method']} {case['path']}"
        if route_key in routes:
            route = merge_route(routes[route_key], route)
        routes[route_key] = route
        probe_route = probe_route_for_case(case)
        if probe_route is not None:
            routes[f"GET {case['probe_path']}"] = probe_route
            app_probe_path = app_route_probe_path(str(case["probe_path"]))
            if app_probe_path is not None:
                routes[f"GET {app_probe_path}"] = probe_route

    for probe_name in ("server.php", "env.php", "headers.php", "router.php"):
        probe_path = f"/_probe/{probe_name}"
        routes.setdefault(
            f"GET {probe_path}",
            {
                "status": 200,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps(default_probe_payload(probe_path), sort_keys=True) + "\n",
            },
        )
    return routes


def write_route_file(docroot: Path, routes: dict[str, Any]) -> None:
    route_dir = docroot / ".apps-matrix"
    route_dir.mkdir(parents=True, exist_ok=True)
    route_file = route_dir / "fixture_routes.json"
    route_file.write_text(json.dumps(routes, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def seed_overlay_assets(docroot: Path, scenario: str, app: str, cases: list[dict[str, Any]]) -> None:
    front_controller_body = ""
    for case in cases:
        raw_path = str(case["path"])
        path = path_without_query(raw_path)
        target = file_path_for_url(docroot, raw_path)
        if has_file_parent(target, docroot):
            continue
        route = route_for_case(scenario, app, case)
        if not front_controller_body and int(route.get("status", 200)) == 200:
            front_controller_body = str(route.get("body", ""))

        if path == "/":
            write_text_if_missing(docroot / "index.php", index_php(str(route["body"])))
            write_text_if_missing(docroot / "index.html", str(route["body"]))
        elif path.endswith("/") and expected_status_for_case(case) in (403, 404):
            target.parent.mkdir(parents=True, exist_ok=True)
            write_text_if_missing(target, "apps-matrix directory fixture\n")
        elif path.lower().endswith((".css", ".js", ".txt", ".ico")):
            write_text_if_missing(target, str(route["body"] or "apps-matrix fixture\n"))
        elif path.lower().endswith((".jpg", ".jpeg", ".png", ".gif", ".webp")):
            write_binary_if_missing(target, b"apps-matrix binary fixture\n")
        elif any(
            marker in path.lower()
            for marker in (
                "wp-config.php",
                ".env",
                ".user.ini",
                "configuration.php",
                "settings.php",
                "config.php",
                "maintenance/run.php",
            )
        ):
            write_text_if_missing(target, "<?php\n// apps-matrix protected fixture\n")

    ensure_front_controller_body(docroot, front_controller_body)

    if scenario.startswith("wordpress"):
        cache_dir = docroot / "wp-content" / "cache-matrix"
        cache_dir.mkdir(parents=True, exist_ok=True)
        write_text_if_missing(cache_dir / "test.css", "apps-matrix css\n")
        write_text_if_missing(cache_dir / "test.js", "apps-matrix js\n")
        write_text_if_missing(cache_dir / "lscache.css", "apps-matrix lscache css\n")
        write_text_if_missing(cache_dir / "wp-optimize.css", "apps-matrix wp optimize css\n")
        write_binary_if_missing(cache_dir / "test-image.jpg", b"apps-matrix jpg\n")
        (cache_dir / "originals").mkdir(parents=True, exist_ok=True)


def probe_paths_for_cases(cases: list[dict[str, Any]]) -> list[str]:
    paths = {f"/_probe/{name}" for name in PROBE_FILES}
    for case in cases:
        probe_path = case.get("probe_path")
        if probe_path:
            paths.add(path_without_query(str(probe_path)))
        raw_path = path_without_query(str(case["path"]))
        if raw_path.startswith("/_probe/"):
            paths.add(raw_path)
    return sorted(paths)


def existing_probe_conflict(
    target_dir: Path,
    managed_dir: Path,
    overwrite: bool,
) -> str | None:
    if overwrite or not target_dir.exists():
        return None
    manifest = managed_dir / "probe_manifest.json"
    if manifest.exists():
        return None
    children = list(target_dir.iterdir())
    if children:
        return f"existing unmanaged probe directory would be overwritten: {target_dir}"
    return None


def write_probe_access_policy(target_dir: Path, mode: str) -> str | None:
    if mode == "enabled":
        return None
    target_dir.mkdir(parents=True, exist_ok=True)
    htaccess = target_dir / ".htaccess"
    if mode == "disabled":
        htaccess.write_text(
            "\n".join(
                [
                    "# Generated by apps-matrix probe hardening.",
                    "Require all denied",
                    "Order deny,allow",
                    "Deny from all",
                    "",
                ]
            ),
            encoding="utf-8",
        )
    elif mode == "local-only":
        htaccess.write_text(
            "\n".join(
                [
                    "# Generated by apps-matrix probe hardening.",
                    "<IfModule mod_authz_core.c>",
                    "  Require ip 127.0.0.1 ::1",
                    "</IfModule>",
                    "<IfModule !mod_authz_core.c>",
                    "  Order deny,allow",
                    "  Deny from all",
                    "  Allow from 127.0.0.1 ::1",
                    "</IfModule>",
                    "",
                ]
            ),
            encoding="utf-8",
        )
    else:
        raise ValueError(f"unknown probe mode: {mode}")
    return str(htaccess)


def copy_probes(
    probes_dir: Path,
    docroot: Path,
    managed_dir: Path,
    cases: list[dict[str, Any]],
    mode: str,
    overwrite: bool,
) -> dict[str, Any]:
    target_dir = docroot / "_probe"
    conflict = existing_probe_conflict(target_dir, managed_dir, overwrite)
    if conflict:
        raise ValueError(conflict)
    managed_dir.mkdir(parents=True, exist_ok=True)
    target_dir.mkdir(parents=True, exist_ok=True)
    copied: list[str] = []
    if mode != "disabled":
        for probe_name in PROBE_FILES:
            probe = probes_dir / probe_name
            if not probe.exists():
                raise ValueError(f"missing probe source: {probe}")
            target = target_dir / probe.name
            if target.exists() and not overwrite and not (managed_dir / "probe_manifest.json").exists():
                raise ValueError(f"refusing to overwrite unmanaged probe file: {target}")
            shutil.copy2(probe, target)
            copied.append(str(target))

    policy = write_probe_access_policy(target_dir, mode)
    manifest = {
        "mode": mode,
        "marker": PROBE_MARKER,
        "probe_dir": str(target_dir),
        "copied": copied,
        "access_policy": policy,
        "expected_paths": probe_paths_for_cases(cases),
        "overwrote_existing": overwrite,
        "generated_at": datetime.now(timezone.utc).isoformat(),
    }
    (managed_dir / "probe_manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return manifest


def write_htaccess(docroot: Path, scenario: str, cases: list[dict[str, Any]]) -> None:
    ttl_values = [ttl for ttl in (ttl_for_case(case) for case in cases) if ttl is not None]
    ttl = ttl_values[0] if ttl_values else TTL_DEFAULT
    lines = [
        "# Generated by apps-matrix fixture provisioning.",
        "# Real app fixtures may replace this with captured application output.",
        "Options -Indexes",
        "DirectoryIndex index.php index.html",
        "RewriteEngine On",
        "",
        "<FilesMatch \"(^\\.|wp-config\\.php|configuration\\.php|settings\\.php|config\\.php|run\\.php|\\.user\\.ini|\\.env)$\">",
        "  Require all denied",
        "</FilesMatch>",
        "",
        "<IfModule mod_headers.c>",
        "  Header set X-Apps-Matrix \"fixture\"",
        "</IfModule>",
        "",
    ]

    for case in cases:
        status = assertion_value(case, "status_exact")
        location = assertion_value(case, "location_exact")
        path = path_without_query(str(case["path"]))
        if location and status in (301, 302):
            redirect_type = "permanent" if int(status) == 301 else "temp"
            if path == "/":
                lines.append(f"RedirectMatch {redirect_type} ^/$ {location}")
            else:
                lines.append(f"Redirect {redirect_type} {path} {location}")
        elif status == 410:
            lines.append(f"Redirect gone {path}")

    if any(ttl_for_case(case) is not None for case in cases):
        lines.extend(
            [
                "",
                "<IfModule mod_expires.c>",
                "  ExpiresActive On",
                f"  ExpiresDefault \"access plus {ttl} seconds\"",
                "</IfModule>",
                "<IfModule mod_headers.c>",
                f"  Header set Cache-Control \"public, max-age={ttl}\"",
                "</IfModule>",
            ]
        )

    lines.extend(
        [
            "",
            "RewriteCond %{REQUEST_FILENAME} !-f",
            "RewriteCond %{REQUEST_FILENAME} !-d",
            "RewriteRule . /index.php [L]",
            "",
            f"# scenario: {scenario}",
        ]
    )
    write_text(docroot / ".htaccess", "\n".join(lines) + "\n")


def write_routes(
    docroot: Path,
    scenario: str,
    app: str,
    cases: list[dict[str, Any]],
) -> dict[str, Any]:
    routes = build_routes(scenario, app, cases)
    front_controller_body = ""
    for case in cases:
        route_key = f"{case['method']} {case['path']}"
        route = routes[route_key]
        if not front_controller_body and int(route.get("status", 200)) == 200:
            front_controller_body = str(route.get("body", ""))
        write_case_file(docroot, case, route)
    ensure_front_controller_body(docroot, front_controller_body)
    write_route_file(docroot, routes)
    return routes


def capture_htaccess(docroot: Path, artifact_dir: Path) -> str | None:
    source = docroot / ".htaccess"
    if not source.exists():
        return None
    artifact_dir.mkdir(parents=True, exist_ok=True)
    target = artifact_dir / "final.htaccess"
    shutil.copy2(source, target)
    return str(target)


def provision_engine(
    engine: str,
    scenario: str,
    app: str,
    cases: list[dict[str, Any]],
    args: argparse.Namespace,
) -> dict[str, Any]:
    docroot, is_custom_docroot = docroot_for(engine, scenario, args.fixture_root)
    if args.mode == "stub":
        clean_generated_docroot(docroot, is_custom_docroot)
    docroot.mkdir(parents=True, exist_ok=True)

    managed_dir = docroot / ".apps-matrix"
    was_managed = (managed_dir / "managed").exists()

    probe_manifest = copy_probes(
        args.probes_dir,
        docroot,
        managed_dir,
        cases,
        args.probe_mode,
        args.probe_overwrite or was_managed,
    )
    managed_dir.mkdir(parents=True, exist_ok=True)
    (managed_dir / "managed").write_text("apps-matrix\n", encoding="utf-8")
    if args.mode == "stub":
        routes = write_routes(docroot, scenario, app, cases)
    else:
        routes = build_routes(scenario, app, cases)
        seed_overlay_assets(docroot, scenario, app, cases)
        write_route_file(docroot, routes)
    if args.mode == "stub" or (
        os.environ.get("MATRIX_WRITE_HTACCESS", "0") == "1"
        and not (docroot / ".htaccess").exists()
    ):
        write_htaccess(docroot, scenario, cases)

    artifact_dir = args.out_dir / "fixtures" / scenario / engine
    captured_htaccess = capture_htaccess(docroot, artifact_dir)
    manifest = {
        "engine": engine,
        "scenario": scenario,
        "app": app,
        "mode": args.mode,
        "docroot": str(docroot),
        "custom_docroot": is_custom_docroot,
        "probes": probe_manifest,
        "routes": len(routes),
        "captured_htaccess": captured_htaccess,
        "generated_at": datetime.now(timezone.utc).isoformat(),
    }
    (managed_dir / "fixture_manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return manifest


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--cases", required=True, type=Path)
    parser.add_argument("--probes-dir", required=True, type=Path)
    parser.add_argument(
        "--mode",
        choices=("stub", "overlay", "real"),
        default=os.environ.get("MATRIX_FIXTURE_MODE", "stub"),
    )
    parser.add_argument(
        "--out-dir",
        default=Path(os.environ.get("MATRIX_OUT_DIR", "out")),
        type=Path,
    )
    parser.add_argument(
        "--fixture-root",
        default=None,
        type=Path,
    )
    parser.add_argument(
        "--probe-mode",
        choices=("enabled", "local-only", "disabled"),
        default=os.environ.get("MATRIX_PROBE_MODE", "enabled"),
        help="probe exposure policy; defaults to enabled for test runs",
    )
    parser.add_argument(
        "--probe-overwrite",
        action="store_true",
        default=os.environ.get("MATRIX_PROBE_OVERWRITE", "0") == "1",
        help="allow replacing an existing unmanaged _probe directory",
    )
    return parser.parse_args()


def main() -> int:
    try:
        args = parse_args()
        args.out_dir = args.out_dir.resolve()
        if args.fixture_root is None:
            args.fixture_root = Path(
                os.environ.get("MATRIX_FIXTURE_ROOT", str(args.out_dir / DEFAULT_FIXTURE_ROOT))
            )
        args.fixture_root = args.fixture_root.resolve()
        meta, cases = load_cases(args.cases)
        app = str(meta.get("app", args.scenario))
        manifests = [
            provision_engine(engine, args.scenario, app, cases, args)
            for engine in selected_engines()
        ]
        scenario_dir = args.out_dir / "fixtures" / args.scenario
        scenario_dir.mkdir(parents=True, exist_ok=True)
        (scenario_dir / "manifest.json").write_text(
            json.dumps(
                {
                    "scenario": args.scenario,
                    "mode": args.mode,
                    "engines": manifests,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        for manifest in manifests:
            print(f"{manifest['engine']} docroot: {manifest['docroot']}")
        return 0
    except (OSError, ValueError) as exc:
        print(f"apps-matrix fixture: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
