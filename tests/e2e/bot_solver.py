#!/usr/bin/env python3
"""E2E helper: drive the BotChallenge JS-PoW gate exactly like the served JS.

Run inside the CI runner against the OLS container:
    python3 tests/e2e/bot_solver.py http://127.0.0.1:8080/bot/index.html

Steps: solve FNV-1a PoW (daily salt, 16 leading zero bits), answer via
?__lhc=<nonce>, verify Set-Cookie + protected content, replay with the
cookie, and confirm a tampered cookie is re-challenged.
"""
import sys
import time
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


def main() -> int:
    base = sys.argv[1].rstrip("/")
    salt = "s%d" % (int(time.time()) // 86400)
    nonce = solve(salt)

    r = urllib.request.urlopen(f"{base}?__lhc={nonce}", timeout=10)
    body = r.read().decode()
    setc = r.headers.get("Set-Cookie", "")
    assert "lhttpd_bot=" in setc, f"no cookie minted: {setc!r}"
    assert "function fnv" not in body and body.strip(), "answer did not pass"
    cookie = setc.split("lhttpd_bot=")[1].split(";")[0]

    req = urllib.request.Request(base)
    req.add_header("Cookie", f"lhttpd_bot={cookie}")
    replay = urllib.request.urlopen(req, timeout=10).read().decode()
    assert "function fnv" not in replay and replay.strip(), "cookie replay failed"

    tampered = cookie[:-1] + ("0" if cookie[-1] != "0" else "1")
    req2 = urllib.request.Request(base)
    req2.add_header("Cookie", f"lhttpd_bot={tampered}")
    rechallenge = urllib.request.urlopen(req2, timeout=10).read().decode()
    assert "function fnv" in rechallenge, "tampered cookie was not re-challenged"

    print("bot_solver: challenge -> PoW -> cookie -> replay -> tamper OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
