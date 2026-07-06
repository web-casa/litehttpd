#!/usr/bin/env python3
"""Execute apps-matrix cases against Apache, OLS module, and native OLS."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from email.utils import parsedate_to_datetime
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlencode, urlparse


ENGINES = ("apache", "ols_module", "ols_native")
RESULTS = ("PASS_EQUIV", "PASS_KNOWN_DIFF", "FAIL_REGRESSION", "FAIL_APP_BROKEN")
DEFAULT_URLS = {
    "apache": "http://localhost:18080",
    "ols_module": "http://localhost:38080",
    "ols_native": "http://localhost:28080",
}
LEGACY_URL_ENV = {
    "apache": "APACHE_URL",
    "ols_module": "OLS_MODULE_URL",
    "ols_native": "OLS_NATIVE_URL",
}


@dataclass
class HttpResponse:
    url: str
    status: int
    headers: dict[str, str]
    body_path: Path
    headers_path: Path
    stderr_path: Path
    duration_ms: int


@dataclass
class EngineRun:
    name: str
    status: int
    headers: dict[str, str]
    location: str
    ttl_seconds: int | None
    body_hash: str
    body_text: str
    probe: dict[str, Any]
    artifacts: dict[str, str]
    duration_ms: int
    probe_duration_ms: int | None

    def as_result(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "status": self.status,
            "headers": self.headers,
            "body_hash": self.body_hash,
            "probe": self.probe,
            "artifacts": self.artifacts,
            "timings": {
                "duration_ms": self.duration_ms,
            },
        }
        if self.probe_duration_ms is not None:
            result["timings"]["probe_duration_ms"] = self.probe_duration_ms
            result["timings"]["total_duration_ms"] = self.duration_ms + self.probe_duration_ms
        if self.location:
            result["location"] = self.location
        if self.ttl_seconds is not None:
            result["ttl_seconds"] = self.ttl_seconds
        return result


def parse_scalar(value: str) -> Any:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
        value = value[1:-1]
    if re.fullmatch(r"-?\d+", value):
        return int(value)
    return value


def parse_kv(text: str, path: Path, line_no: int) -> tuple[str, Any]:
    if ":" not in text:
        raise ValueError(f"{path}:{line_no}: expected key: value")
    key, value = text.split(":", 1)
    key = key.strip()
    if not key:
        raise ValueError(f"{path}:{line_no}: empty key")
    return key, parse_scalar(value)


def load_cases(path: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    meta: dict[str, Any] = {}
    cases: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    in_cases = False

    for line_no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue

        stripped = raw.strip()
        if stripped == "cases:":
            in_cases = True
            continue

        if not in_cases:
            key, value = parse_kv(stripped, path, line_no)
            meta[key] = value
            continue

        if raw.startswith("  - "):
            if current is not None:
                cases.append(current)
            current = {"assert": []}
            remainder = raw[4:].strip()
            if remainder:
                key, value = parse_kv(remainder, path, line_no)
                current[key] = value
            continue

        if current is None:
            raise ValueError(f"{path}:{line_no}: case field before first case")

        if raw.startswith("    assert:"):
            current.setdefault("assert", [])
            continue

        if raw.startswith("      - "):
            key, value = parse_kv(stripped[2:].strip(), path, line_no)
            current.setdefault("assert", []).append({key: value})
            continue

        if raw.startswith("    "):
            key, value = parse_kv(stripped, path, line_no)
            current[key] = value
            continue

        raise ValueError(f"{path}:{line_no}: unsupported YAML shape")

    if current is not None:
        cases.append(current)

    for case in cases:
        for required in ("id", "category", "desc", "method", "path"):
            if required not in case:
                raise ValueError(f"{path}: case missing required field {required}")
        if not case.get("assert"):
            raise ValueError(f"{path}: case {case['id']} has no assertions")

    return meta, cases


def selected_engines() -> tuple[str, ...]:
    raw = os.environ.get("MATRIX_ENGINES", ",".join(ENGINES))
    engines = tuple(engine.strip() for engine in raw.split(",") if engine.strip())
    unknown = sorted(set(engines) - set(ENGINES))
    if unknown:
        raise ValueError(f"unknown MATRIX_ENGINES value(s): {', '.join(unknown)}")
    for required in ("apache", "ols_module"):
        if required not in engines:
            raise ValueError("MATRIX_ENGINES must include apache and ols_module")
    return engines


def engine_urls() -> dict[str, str]:
    urls = {}
    for engine in ENGINES:
        matrix_env = f"MATRIX_{engine.upper()}_URL"
        urls[engine] = (
            os.environ.get(matrix_env)
            or os.environ.get(LEGACY_URL_ENV[engine])
            or DEFAULT_URLS[engine]
        )
    return urls


def make_url(base: str, path: str) -> str:
    if not path.startswith("/"):
        path = f"/{path}"
    return f"{base.rstrip('/')}{path}"


def app_route_probe_path(probe_path: str) -> str:
    if os.environ.get("MATRIX_APP_ROUTE_PROBE", "0") != "1":
        return probe_path

    parsed = urlparse(probe_path)
    if parsed.path != "/_probe/router.php":
        return probe_path

    target_uri = parse_qs(parsed.query).get("uri", [""])[0]
    if not target_uri.startswith("/"):
        return probe_path

    separator = "&" if "?" in target_uri else "?"
    return f"{target_uri}{separator}{urlencode({'__apps_matrix_probe': 'router'})}"


def parse_headers(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    raw = path.read_text(encoding="iso-8859-1", errors="replace")
    blocks = [block for block in re.split(r"\r?\n\r?\n", raw) if block.strip()]
    if not blocks:
        return {}

    block = blocks[-1]
    for candidate in reversed(blocks):
        if candidate.lstrip().startswith("HTTP/"):
            block = candidate
            break

    headers: dict[str, str] = {}
    for line in block.splitlines()[1:]:
        if ":" not in line:
            continue
        name, value = line.split(":", 1)
        name = name.strip()
        value = value.strip().rstrip("\r")
        if name in headers:
            headers[name] = f"{headers[name]}, {value}"
        else:
            headers[name] = value
    return headers


def header_value(headers: dict[str, str], name: str) -> str | None:
    for header_name, value in headers.items():
        if header_name.lower() == name.lower():
            return value
    return None


def ttl_from_headers(headers: dict[str, str]) -> int | None:
    cache_control = header_value(headers, "Cache-Control")
    if cache_control:
        match = re.search(r"(?:^|,)\s*max-age=(\d+)", cache_control, re.IGNORECASE)
        if match:
            return int(match.group(1))

    expires = header_value(headers, "Expires")
    date = header_value(headers, "Date")
    if not expires or not date:
        return None
    try:
        expires_at = parsedate_to_datetime(expires)
        date_at = parsedate_to_datetime(date)
    except (TypeError, ValueError, IndexError, OverflowError):
        return None
    return max(0, int((expires_at - date_at).total_seconds()))


def body_hash(path: Path) -> str:
    if not path.exists():
        return hashlib.sha256(b"").hexdigest()
    return hashlib.sha256(path.read_bytes()).hexdigest()


def body_text(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def parse_json_body(path: Path) -> dict[str, Any]:
    text = body_text(path).strip()
    if not text:
        return {}
    try:
        parsed = json.loads(text)
    except json.JSONDecodeError:
        return {"_raw": text}
    if isinstance(parsed, dict):
        return parsed
    return {"_value": parsed}


def run_curl(method: str, url: str, out_dir: Path, prefix: str, timeout: int) -> HttpResponse:
    out_dir.mkdir(parents=True, exist_ok=True)
    body_path = out_dir / f"{prefix}body.txt"
    headers_path = out_dir / f"{prefix}headers.txt"
    status_path = out_dir / f"{prefix}status.txt"
    stderr_path = out_dir / f"{prefix}curl_stderr.txt"
    request_path = out_dir / f"{prefix}request.txt"

    request_path.write_text(f"{method} {url}\n", encoding="utf-8")
    cmd = [
        "curl",
        "-sS",
        "--path-as-is",
        "-X",
        method,
        "--max-time",
        str(timeout),
        "-D",
        str(headers_path),
        "-o",
        str(body_path),
        "-w",
        "%{http_code}",
    ]
    host_header = os.environ.get("MATRIX_HTTP_HOST", "").strip()
    if host_header:
        if "\r" in host_header or "\n" in host_header:
            raise ValueError("MATRIX_HTTP_HOST must not contain newlines")
        cmd.extend(["-H", f"Host: {host_header}"])
    cmd.append(url)
    started = time.perf_counter()
    proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    duration_ms = int((time.perf_counter() - started) * 1000)
    stderr_path.write_text(proc.stderr, encoding="utf-8")

    status_text = proc.stdout.strip()
    status = int(status_text) if re.fullmatch(r"\d{3}", status_text) else 0
    if proc.returncode != 0 and status == 0:
        status = 0
    status_path.write_text(f"{status}\n", encoding="utf-8")
    body_path.touch(exist_ok=True)
    headers_path.touch(exist_ok=True)

    return HttpResponse(
        url=url,
        status=status,
        headers=parse_headers(headers_path),
        body_path=body_path,
        headers_path=headers_path,
        stderr_path=stderr_path,
        duration_ms=duration_ms,
    )


def execute_case(
    scenario: str,
    case: dict[str, Any],
    engine: str,
    base_url: str,
    out_dir: Path,
    timeout: int,
) -> EngineRun:
    case_id = str(case["id"])
    artifact_dir = out_dir / "artifacts" / scenario / case_id / engine
    method = str(case["method"])
    main = run_curl(method, make_url(base_url, str(case["path"])), artifact_dir, "", timeout)
    probe_response: HttpResponse | None = None
    if case.get("probe_path"):
        probe_path = app_route_probe_path(str(case["probe_path"]))
        probe_response = run_curl(
            "GET",
            make_url(base_url, probe_path),
            artifact_dir,
            "probe_",
            timeout,
        )

    probe = parse_json_body(probe_response.body_path) if probe_response else {}
    if not probe and case_uses_probe(case):
        probe = parse_json_body(main.body_path)

    ttl = ttl_from_headers(main.headers)
    probe_duration_ms = probe_response.duration_ms if probe_response else None
    return EngineRun(
        name=engine,
        status=main.status,
        headers=main.headers,
        location=header_value(main.headers, "Location") or "",
        ttl_seconds=ttl,
        body_hash=body_hash(main.body_path),
        body_text=body_text(main.body_path),
        probe=probe,
        artifacts={
            "body": str(main.body_path),
            "headers": str(main.headers_path),
            "stderr": str(main.stderr_path),
        },
        duration_ms=main.duration_ms,
        probe_duration_ms=probe_duration_ms,
    )


def case_uses_probe(case: dict[str, Any]) -> bool:
    for assertion in case.get("assert", []):
        name = next(iter(assertion.keys()))
        if name.startswith("probe_field_"):
            return True
    return False


def split_name_value(value: Any) -> tuple[str, str]:
    text = str(value)
    if "=" not in text:
        raise ValueError(f"expected NAME=value assertion, got {text!r}")
    name, expected = text.split("=", 1)
    return name.strip(), expected.strip()


def split_int_list(value: Any) -> set[int]:
    values: set[int] = set()
    for item in str(value).split(","):
        item = item.strip()
        if not item:
            continue
        values.add(int(item))
    if not values:
        raise ValueError(f"expected comma-separated status list, got {value!r}")
    return values


def normalized_location(actual: str, expected: str) -> str:
    if expected.startswith("/"):
        parsed = urlparse(actual)
        if parsed.scheme and parsed.netloc:
            path = parsed.path or "/"
            return f"{path}?{parsed.query}" if parsed.query else path
    return actual


def probe_value(probe: dict[str, Any], name: str) -> Any:
    if name in probe:
        return probe[name]
    for key, value in probe.items():
        if key.lower() == name.lower():
            return value
    return None


def evaluate_assertions(case: dict[str, Any], run: EngineRun, ttl_skew: int) -> list[str]:
    failures: list[str] = []
    for assertion in case.get("assert", []):
        if len(assertion) != 1:
            failures.append(f"malformed assertion: {assertion!r}")
            continue
        name, expected = next(iter(assertion.items()))

        try:
            if name == "status_exact":
                if run.status != int(expected):
                    failures.append(f"status_exact expected {expected}, got {run.status}")
            elif name == "status_in":
                expected_statuses = split_int_list(expected)
                if run.status not in expected_statuses:
                    expected_text = ",".join(str(status) for status in sorted(expected_statuses))
                    failures.append(f"status_in expected one of {expected_text}, got {run.status}")
            elif name == "location_exact":
                actual = normalized_location(run.location, str(expected))
                if actual != str(expected):
                    failures.append(f"location_exact expected {expected!r}, got {actual!r}")
            elif name == "header_exact":
                header, expected_value = split_name_value(expected)
                actual = header_value(run.headers, header)
                if actual != expected_value:
                    failures.append(
                        f"header_exact {header} expected {expected_value!r}, got {actual!r}"
                    )
            elif name == "header_contains":
                header, expected_value = split_name_value(expected)
                actual = header_value(run.headers, header)
                if actual is None or expected_value.lower() not in actual.lower():
                    failures.append(
                        f"header_contains {header} expected {expected_value!r}, got {actual!r}"
                    )
            elif name == "header_absent":
                actual = header_value(run.headers, str(expected))
                if actual is not None:
                    failures.append(f"header_absent {expected} got {actual!r}")
            elif name == "body_contains":
                if str(expected).lower() not in run.body_text.lower():
                    failures.append(f"body_contains expected {expected!r}")
            elif name == "body_not_contains":
                if str(expected).lower() in run.body_text.lower():
                    failures.append(f"body_not_contains found {expected!r}")
            elif name == "probe_field_exact":
                field, expected_value = split_name_value(expected)
                actual = probe_value(run.probe, field)
                if str(actual) != expected_value:
                    failures.append(
                        f"probe_field_exact {field} expected {expected_value!r}, got {actual!r}"
                    )
            elif name == "probe_field_contains":
                field, expected_value = split_name_value(expected)
                actual = probe_value(run.probe, field)
                if actual is None or expected_value not in str(actual):
                    failures.append(
                        f"probe_field_contains {field} expected {expected_value!r}, got {actual!r}"
                    )
            elif name == "ttl_close":
                if run.ttl_seconds is None:
                    failures.append(f"ttl_close expected {expected}, got no TTL")
                elif abs(run.ttl_seconds - int(expected)) > ttl_skew:
                    failures.append(
                        f"ttl_close expected {expected}+/-{ttl_skew}, got {run.ttl_seconds}"
                    )
            else:
                failures.append(f"unsupported assertion {name}")
        except ValueError as exc:
            failures.append(str(exc))

    return failures


def classify_case(
    failures: dict[str, list[str]],
    engines: tuple[str, ...],
) -> tuple[str, str, str]:
    if failures.get("apache"):
        return "FAIL_APP_BROKEN", "", ""
    if failures.get("ols_module"):
        return "FAIL_REGRESSION", "", ""
    if "ols_native" in engines and failures.get("ols_native"):
        return (
            "PASS_KNOWN_DIFF",
            "ols_native differs from apache/module for this .htaccess/app behavior",
            "low",
        )
    return "PASS_EQUIV", "", ""


def ensure_csv_header(path: Path, header: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.stat().st_size > 0:
        return
    with path.open("w", encoding="utf-8", newline="") as handle:
        csv.writer(handle, lineterminator="\n").writerow(header)


def append_csv(path: Path, row: list[str]) -> None:
    with path.open("a", encoding="utf-8", newline="") as handle:
        csv.writer(handle, lineterminator="\n").writerow(row)


def skipped_engine_result() -> dict[str, Any]:
    return {
        "status": 0,
        "headers": {},
        "body_hash": hashlib.sha256(b"").hexdigest(),
        "probe": {},
        "skipped": True,
        "timings": {
            "duration_ms": 0,
        },
    }


def write_case_result(
    out_dir: Path,
    scenario: str,
    case: dict[str, Any],
    runs: dict[str, EngineRun],
    failures: dict[str, list[str]],
    result: str,
    known_diff_reason: str,
    risk_level: str,
) -> None:
    result_dir = out_dir / "results" / scenario
    result_dir.mkdir(parents=True, exist_ok=True)
    payload: dict[str, Any] = {
        "scenario": scenario,
        "case_id": case["id"],
        "category": case["category"],
        "description": case["desc"],
        "apache": runs["apache"].as_result() if "apache" in runs else skipped_engine_result(),
        "ols_module": (
            runs["ols_module"].as_result() if "ols_module" in runs else skipped_engine_result()
        ),
        "ols_native": (
            runs["ols_native"].as_result() if "ols_native" in runs else skipped_engine_result()
        ),
        "result": result,
        "failures": failures,
    }
    if known_diff_reason:
        payload["known_diff_reason"] = known_diff_reason
    if risk_level:
        payload["risk_level"] = risk_level

    validate_result_payload(payload)
    result_path = result_dir / f"{case['id']}.json"
    result_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def validate_engine_result(engine: str, payload: Any) -> None:
    if not isinstance(payload, dict):
        raise ValueError(f"{engine} must be an object")
    status = payload.get("status")
    if not isinstance(status, int):
        raise ValueError(f"{engine}.status must be an integer")
    headers = payload.get("headers")
    if not isinstance(headers, dict):
        raise ValueError(f"{engine}.headers must be an object")
    probe = payload.get("probe")
    if probe is not None and not isinstance(probe, dict):
        raise ValueError(f"{engine}.probe must be an object")
    timings = payload.get("timings")
    if timings is not None and not isinstance(timings, dict):
        raise ValueError(f"{engine}.timings must be an object")
    if isinstance(timings, dict):
        for key in ("duration_ms", "probe_duration_ms", "total_duration_ms"):
            if key in timings and not isinstance(timings[key], int):
                raise ValueError(f"{engine}.timings.{key} must be an integer")


def validate_result_payload(payload: dict[str, Any]) -> None:
    required = ("scenario", "case_id", "category", "apache", "ols_module", "ols_native", "result")
    for key in required:
        if key not in payload:
            raise ValueError(f"missing required result field: {key}")
    if payload["result"] not in RESULTS:
        raise ValueError(f"invalid result: {payload['result']}")
    for engine in ENGINES:
        validate_engine_result(engine, payload[engine])
    if payload["result"] == "PASS_KNOWN_DIFF":
        if not payload.get("known_diff_reason"):
            raise ValueError("PASS_KNOWN_DIFF requires known_diff_reason")
        if payload.get("risk_level") not in ("low", "medium", "high"):
            raise ValueError("PASS_KNOWN_DIFF requires low/medium/high risk_level")


def selected_case_ids(args: argparse.Namespace) -> set[str]:
    selected: set[str] = set(args.case_id or [])
    env_value = os.environ.get("MATRIX_CASE_IDS", "")
    for case_id in env_value.split(","):
        case_id = case_id.strip()
        if case_id:
            selected.add(case_id)
    return selected


def filter_cases(cases: list[dict[str, Any]], case_ids: set[str]) -> list[dict[str, Any]]:
    if not case_ids:
        return cases
    filtered = [case for case in cases if str(case["id"]) in case_ids]
    missing = sorted(case_ids - {str(case["id"]) for case in filtered})
    if missing:
        raise ValueError(f"case id(s) not found: {', '.join(missing)}")
    return filtered


def optional_int_env(name: str) -> int | None:
    value = os.environ.get(name, "").strip()
    if not value:
        return None
    try:
        return int(value)
    except ValueError as exc:
        raise ValueError(f"{name} must be an integer") from exc


def engine_total_ms(run: EngineRun) -> int:
    return run.duration_ms + (run.probe_duration_ms or 0)


def record_performance(
    path: Path,
    scenario: str,
    case: dict[str, Any],
    engine: str,
    run: EngineRun,
    budget_ms: int | None,
) -> str:
    total_ms = engine_total_ms(run)
    status = ""
    if budget_ms is not None:
        status = "over_budget" if total_ms > budget_ms else "ok"
    append_csv(
        path,
        [
            scenario,
            str(case["id"]),
            str(case["category"]),
            engine,
            str(run.duration_ms),
            str(run.probe_duration_ms or 0),
            str(total_ms),
            "" if budget_ms is None else str(budget_ms),
            status,
        ],
    )
    return status


def execute_cases(args: argparse.Namespace) -> int:
    if shutil.which("curl") is None:
        print("curl is required for apps-matrix execution", file=sys.stderr)
        return 1

    meta, cases = load_cases(args.cases)
    cases = filter_cases(cases, selected_case_ids(args))
    if not cases:
        raise ValueError("no cases selected")
    scenario = args.scenario or str(meta.get("scenario", ""))
    if not scenario:
        print("scenario is required", file=sys.stderr)
        return 1

    engines = selected_engines()
    urls = engine_urls()
    out_dir: Path = args.out_dir
    timeout = int(os.environ.get("MATRIX_CURL_TIMEOUT", "20"))
    ttl_skew = int(os.environ.get("MATRIX_TTL_SKEW", "5"))
    per_engine_budget_ms = optional_int_env("MATRIX_PER_ENGINE_BUDGET_MS")
    enforce_perf_budget = os.environ.get("MATRIX_ENFORCE_PERF_BUDGET", "0") == "1"

    ensure_csv_header(
        args.summary,
        ["scenario", "case_id", "category", "result", "known_diff_reason"],
    )
    known_diff_csv = out_dir / "known_diff.csv"
    failures_csv = out_dir / "failures.csv"
    performance_csv = out_dir / "performance.csv"
    ensure_csv_header(
        known_diff_csv,
        ["scenario", "case_id", "category", "engine", "reason", "risk_level"],
    )
    ensure_csv_header(failures_csv, ["scenario", "case_id", "category", "engine", "failure"])
    ensure_csv_header(
        performance_csv,
        [
            "scenario",
            "case_id",
            "category",
            "engine",
            "duration_ms",
            "probe_duration_ms",
            "total_duration_ms",
            "budget_ms",
            "budget_status",
        ],
    )

    had_failure = False
    for case in cases:
        runs: dict[str, EngineRun] = {}
        failures: dict[str, list[str]] = {}
        for engine in engines:
            run = execute_case(scenario, case, engine, urls[engine], out_dir, timeout)
            runs[engine] = run
            failures[engine] = evaluate_assertions(case, run, ttl_skew)
            budget_status = record_performance(
                performance_csv,
                scenario,
                case,
                engine,
                run,
                per_engine_budget_ms,
            )
            if enforce_perf_budget and budget_status == "over_budget":
                failures[engine].append(
                    f"performance budget exceeded: {engine_total_ms(run)}ms > {per_engine_budget_ms}ms"
                )
        for engine in ENGINES:
            failures.setdefault(engine, [])

        result, known_diff_reason, risk_level = classify_case(failures, engines)
        had_failure = had_failure or result.startswith("FAIL_")
        append_csv(
            args.summary,
            [
                scenario,
                str(case["id"]),
                str(case["category"]),
                result,
                known_diff_reason,
            ],
        )
        if result == "PASS_KNOWN_DIFF":
            append_csv(
                known_diff_csv,
                [
                    scenario,
                    str(case["id"]),
                    str(case["category"]),
                    "ols_native",
                    known_diff_reason,
                    risk_level,
                ],
            )
        if result.startswith("FAIL_"):
            for engine, engine_failures in failures.items():
                for failure in engine_failures:
                    append_csv(
                        failures_csv,
                        [scenario, str(case["id"]), str(case["category"]), engine, failure],
                    )

        write_case_result(
            out_dir,
            scenario,
            case,
            runs,
            failures,
            result,
            known_diff_reason,
            risk_level,
        )
        print(f"{scenario}/{case['id']}: {result}")

    allow_failures = os.environ.get("MATRIX_ALLOW_FAILURES", "0") == "1"
    return 0 if allow_failures or not had_failure else 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--cases", required=True, type=Path)
    parser.add_argument("--case-id", action="append", default=[])
    parser.add_argument("--summary", required=True, type=Path)
    parser.add_argument(
        "--out-dir",
        default=Path(os.environ.get("MATRIX_OUT_DIR", "out")),
        type=Path,
    )
    return parser.parse_args()


def main() -> int:
    try:
        return execute_cases(parse_args())
    except (OSError, ValueError) as exc:
        print(f"apps-matrix: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
