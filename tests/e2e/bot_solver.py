#!/usr/bin/env python3
"""E2E helper: drive the BotChallenge JS-PoW gate exactly like the served JS.

Run inside the CI runner against the OLS container:
    python3 tests/e2e/bot_solver.py http://127.0.0.1:8080/bot/index.html
    python3 tests/e2e/bot_solver.py --xff http://127.0.0.1:8080/xff/index.html

Steps: solve FNV-1a PoW (daily salt, 16 leading zero bits), answer via
?__lhc=<nonce>. The gate answers with a 302 to the CLEAN URL plus the
credential cookie — exactly like a browser, a cookie-aware opener follows
it and must land on the protected content (never on a __lhc URL). Then
replay with the cookie, and confirm a tampered cookie is re-challenged.
"""
import http.cookiejar
import sys
import time
import urllib.error
import urllib.request


def fnv1a(s: str) -> int:
    h = 2166136261
    for ch in s.encode():
        h ^= ch
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def solve(salt: str, difficulty: int = 16) -> int:
    n = 0
    shift = 32 - difficulty
    while True:
        if fnv1a(f"{salt}:{n}") >> shift == 0:
            return n
        n += 1


class Client:
    """Cookie-aware HTTP client (browser stand-in). Exposes the last
    response so callers can inspect the 302's Location and Set-Cookie."""

    def __init__(self):
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(self.jar)
        )

    def open(self, url: str, headers: dict | None = None,
             follow: bool = True):
        req = urllib.request.Request(url)
        for k, v in (headers or {}).items():
            req.add_header(k, v)
        if follow:
            return self.opener.open(req, timeout=10)
        # manual redirect handling: capture the 302 itself
        try:
            return self.opener.open(req, timeout=10)
        except urllib.error.HTTPError as e:
            return e

    def cookie_value(self) -> str:
        for c in self.jar:
            if c.name == "lhttpd_bot":
                return c.value
        raise AssertionError("no lhttpd_bot cookie in jar")


def plain_open(url: str, headers: dict | None = None):
    req = urllib.request.Request(url)
    for k, v in (headers or {}).items():
        req.add_header(k, v)
    return urllib.request.urlopen(req, timeout=10)


def xff_mode(base: str) -> int:
    """Trusted-proxy variant: mint through an X-Forwarded-For header,
    replay from another host in the SAME /24 (must pass), and from a
    DIFFERENT class (must be re-challenged)."""
    salt = "s%d" % (int(time.time()) // 86400)
    nonce = solve(salt)

    def get(u: str, xff: str, cookie: str | None = None):
        hdr = {"X-Forwarded-For": xff}
        if cookie:
            hdr["Cookie"] = f"lhttpd_bot={cookie}"
        return plain_open(u, hdr)

    # the answer 302s to the clean URL; capture the cookie off the 302
    c = Client()
    r302 = c.open(f"{base}?__lhc={nonce}",
                  headers={"X-Forwarded-For": "1.2.3.4, 203.0.113.5"})
    assert "fnv" not in r302.read().decode(), "answer did not pass"
    cookie = c.cookie_value()

    same = get(base, "203.0.113.99", cookie).read().decode()
    assert "fnv" not in same and same.strip(), "same-class replay failed"

    cross = get(base, "198.51.100.1", cookie).read().decode()
    assert "fnv" in cross, "cross-class cookie was not re-challenged"
    print("bot_solver --xff: mint/replay/cross-class OK")
    return 0


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    xff = "--xff" in sys.argv
    base = (args[0] if args else "").rstrip("/")
    if not base:
        print("usage: bot_solver.py [--xff] URL", file=sys.stderr)
        return 2
    if xff:
        return xff_mode(base)
    salt = "s%d" % (int(time.time()) // 86400)
    nonce = solve(salt)

    c = Client()
    # answer → 302 to the clean URL with the cookie; following it must
    # land on the content (browser-equivalent flow)
    r = c.open(f"{base}?__lhc={nonce}")
    body = r.read().decode()
    assert "function fnv" not in body and body.strip(), "answer did not pass"
    assert "__lhc" not in r.url, f"landed on {r.url}"
    cookie = c.cookie_value()

    replay = plain_open(base, {"Cookie": f"lhttpd_bot={cookie}"}).read().decode()
    assert "function fnv" not in replay and replay.strip(), "cookie replay failed"

    tampered = cookie[:-1] + ("0" if cookie[-1] != "0" else "1")
    rechallenge = plain_open(
        base, {"Cookie": f"lhttpd_bot={tampered}"}).read().decode()
    assert "function fnv" in rechallenge, "tampered cookie was not re-challenged"

    print("bot_solver: challenge -> PoW -> 302-clean-URL -> replay -> tamper OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
