#!/usr/bin/env python3
"""Checks the board's app API (firmware: components/board_api) from outside,
against the real board or tool/fake_board.py. Exit code 0 = all passed.

Default: safe for the real door. Only reads, and sends values the board must
refuse (bad token, out-of-range settings, bad ids); nothing changes.
--full also enrolls a face, deletes it, unlocks the door and changes then
restores settings: use it only against the fake board (CI does).

  python3 tool/api_check.py --host 192.168.100.75 --token AB9A9JQ2
  python3 tool/api_check.py --host 127.0.0.1:8080 --token AB9A9JQ2 --full

Standard library only.
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request

failures = []


def call(base, method, path, token, body=None, raw_body=None):
    """(status, parsed JSON or raw bytes)."""
    data = raw_body if raw_body is not None else None
    if body is not None:
        data = json.dumps(body).encode()
    headers = {"Content-Type": "application/json"}
    if token is not None:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(base + path, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=5) as response:
            status, content = response.status, response.read()
    except urllib.error.HTTPError as error:
        status, content = error.code, error.read()
    try:
        return status, json.loads(content) if content else None
    except ValueError:
        return status, content


def check(label, is_ok, detail: object = ""):
    print(("  ok   " if is_ok else "  FAIL ") + label + ("" if is_ok else f"  ({detail})"))
    if not is_ok:
        failures.append(label)


def expect_status(base, token, label, method, path, expected, body=None, raw_body=None):
    status, content = call(base, method, path, token, body, raw_body)
    check(f"{label}: {method} {path} -> {expected}", status == expected, f"got {status} {content!r}")
    return content


def wait_until_up(base, token):
    for _ in range(25):
        try:
            call(base, "GET", "/api/status", token)
            return
        except OSError:
            time.sleep(0.2)
    sys.exit(f"no answer from {base}")


def safe_checks(api, stream, token):
    print("token")
    expect_status(api, None, "no token", "GET", "/api/status", 401)
    expect_status(api, "WRONG123", "wrong token", "GET", "/api/status", 401)
    expect_status(stream, None, "stream needs token", "GET", "/jpg", 401)
    status, content = call(stream, "GET", "/jpg", token)
    check("snapshot with token is a JPEG", status == 200 and content[:2] == b"\xff\xd8", f"got {status}")

    print("reads")
    content = expect_status(api, token, "status", "GET", "/api/status", 200)
    for key in ("version", "recognition", "state", "door", "waiting_request", "ip"):
        check(f"status has {key}", isinstance(content, dict) and key in content, content)
    content = expect_status(api, token, "events", "GET", "/api/events?after=0", 200)
    check("events have last_seq", isinstance(content, dict) and isinstance(content.get("last_seq"), int), content)
    content = expect_status(api, token, "faces", "GET", "/api/faces", 200)
    check("faces is a list", isinstance(content, dict) and isinstance(content.get("faces"), list), content)
    expect_status(api, token, "rules", "GET", "/api/rules", 200)
    expect_status(api, token, "camera", "GET", "/api/camera", 200)

    print("refused input (nothing changes)")
    expect_status(api, token, "hold too short", "PUT", "/api/rules", 400, {"unlock_hold_ms": 100})
    expect_status(api, token, "similarity way out", "PUT", "/api/rules", 400, {"min_similarity_percent": 300})
    expect_status(api, token, "rules not JSON", "PUT", "/api/rules", 400, raw_body=b"{nope")
    expect_status(api, token, "denoise too high", "PUT", "/api/camera", 400, {"denoise": 9})
    expect_status(api, token, "flip not a bool", "PUT", "/api/camera", 400, {"vflip": 1})
    expect_status(api, token, "camera not JSON", "PUT", "/api/camera", 400, raw_body=b"[")
    expect_status(api, token, "empty name", "POST", "/api/faces/enroll", 400, {"name": ""})
    expect_status(api, token, "name too long", "POST", "/api/faces/enroll", 400, {"name": "x" * 40})
    expect_status(api, token, "approval id 0", "POST", "/api/approval", 400, {"request": 0, "approve": True})
    expect_status(api, token, "face id 0", "DELETE", "/api/faces/0", 400)
    expect_status(api, token, "face id text", "DELETE", "/api/faces/abc", 400)
    expect_status(api, token, "unknown face", "DELETE", "/api/faces/65000", 404)


def full_checks(api, token):
    print("changes (fake board only)")
    rules = call(api, "GET", "/api/rules", token)[1]
    content = expect_status(api, token, "rules save", "PUT", "/api/rules", 200, {"unlock_hold_ms": 4500})
    check("rules saved value comes back", content.get("unlock_hold_ms") == 4500, content)
    expect_status(api, token, "rules restore", "PUT", "/api/rules", 200, rules)
    camera = call(api, "GET", "/api/camera", token)[1]
    content = expect_status(api, token, "camera save", "PUT", "/api/camera", 200, {"brightness": 1})
    check("camera saved value comes back", content.get("brightness") == 1, content)
    expect_status(api, token, "camera restore", "PUT", "/api/camera", 200, camera)

    expect_status(api, token, "enroll", "POST", "/api/faces/enroll", 202, {"name": "check"})
    face = None
    for _ in range(50):
        faces = call(api, "GET", "/api/faces", token)[1]["faces"]
        face = next((f for f in faces if f["name"] == "check"), None)
        if face:
            break
        time.sleep(0.2)
    check("enrolled face shows up", face is not None)
    if face:
        expect_status(api, token, "delete enrolled", "DELETE", f"/api/faces/{face['id']}", 202)
    expect_status(api, token, "unlock", "POST", "/api/door/unlock", 202)
    check("door opens", call(api, "GET", "/api/status", token)[1]["door"] == "open")


def main():
    parser = argparse.ArgumentParser(description="Check the board's app API")
    parser.add_argument("--host", required=True, help="board IP, or host:port of the API")
    parser.add_argument("--token", required=True)
    parser.add_argument("--full", action="store_true", help="also change things (fake board only)")
    args = parser.parse_args()

    host, _, port = args.host.partition(":")
    api_port = int(port or 80)
    api = f"http://{host}:{api_port}"
    stream = f"http://{host}:{api_port + 1}"
    wait_until_up(api, args.token)
    safe_checks(api, stream, args.token)
    if args.full:
        full_checks(api, args.token)
    print(f"\n{'FAILED: ' + str(len(failures)) if failures else 'all passed'}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
