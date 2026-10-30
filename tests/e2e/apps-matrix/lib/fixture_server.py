#!/usr/bin/env python3
"""Serve a generated apps-matrix fixture route map for harness smoke tests."""

from __future__ import annotations

import argparse
import json
import mimetypes
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse


class FixtureHandler(BaseHTTPRequestHandler):
    routes: dict[str, dict[str, object]] = {}
    docroot: Path

    def do_GET(self) -> None:
        self.serve_request(include_body=True)

    def do_HEAD(self) -> None:
        self.serve_request(include_body=False)

    def log_message(self, fmt: str, *args: object) -> None:
        sys.stderr.write("fixture-server: " + fmt % args + "\n")

    def serve_request(self, include_body: bool) -> None:
        route = self.lookup_route()
        if route is not None:
            self.send_route(route, include_body)
            return
        self.send_static(include_body)

    def lookup_route(self) -> dict[str, object] | None:
        method = "GET" if self.command == "HEAD" else self.command
        parsed = urlparse(self.path)
        candidates = [
            f"{method} {self.path}",
            f"{method} {parsed.path}",
            f"GET {self.path}",
            f"GET {parsed.path}",
        ]
        for key in candidates:
            route = self.routes.get(key)
            if route is not None:
                return route
        return None

    def send_route(self, route: dict[str, object], include_body: bool) -> None:
        status = int(route.get("status", 200))
        body = str(route.get("body", "")).encode("utf-8")
        headers = dict(route.get("headers", {}))
        self.send_response(status)
        for name, value in headers.items():
            self.send_header(str(name), str(value))
        if "Content-Length" not in headers:
            self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if include_body and body:
            self.wfile.write(body)

    def send_static(self, include_body: bool) -> None:
        parsed = urlparse(self.path)
        relative = parsed.path.lstrip("/")
        if not relative:
            relative = "index.html"
        target = (self.docroot / relative).resolve()
        try:
            target.relative_to(self.docroot)
        except ValueError:
            self.send_error(403)
            return
        if target.is_dir():
            self.send_error(403)
            return
        if not target.exists():
            self.send_error(404)
            return
        body = target.read_bytes()
        self.send_response(200)
        content_type = mimetypes.guess_type(str(target))[0] or "application/octet-stream"
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if include_body:
            self.wfile.write(body)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--docroot", required=True, type=Path)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", required=True, type=int)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    docroot = args.docroot.resolve()
    route_file = docroot / ".apps-matrix" / "fixture_routes.json"
    if not route_file.exists():
        print(f"missing route file: {route_file}", file=sys.stderr)
        return 1
    FixtureHandler.docroot = docroot
    FixtureHandler.routes = json.loads(route_file.read_text(encoding="utf-8"))
    server = ThreadingHTTPServer((args.host, args.port), FixtureHandler)
    print(f"serving {docroot} on http://{args.host}:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
