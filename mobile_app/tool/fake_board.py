#!/usr/bin/env python3
"""Fake face-unlock board: serves the same HTTP endpoints as the firmware,
so the app can be tested without hardware.

  port N    (firmware: board_api, port 80)
    GET    /api/status, /api/events?after=N, /api/faces, /api/rules, /api/camera
    POST   /api/faces/enroll {name}  (a fake face is "seen" after 3 s)
    POST   /api/faces/enroll/cancel, /api/door/unlock (relocks after 3 s),
           /api/approval {request, approve}
    DELETE /api/faces/<id>
    PUT    /api/rules, /api/camera   (only the keys sent change)
    POST   /fake/visitor             fake only: a known face asks for approval
  port N+1  (firmware: video_stream, port 81)
    /        small viewer page; open /?token=<token> in a browser
    /stream  MJPEG, same part format and boundary as the firmware
    /jpg     one frame

Every endpoint except / needs the token, as "Authorization: Bearer <token>"
or "?token=<token>", like the firmware. Each port answers one request at a
time, like esp_http_server: while someone watches /stream, the stream port
does not answer, but the API port does.

Frames are the JPEG files in tool/fake_frames/, played in a loop.
Standard library only.

  python3 tool/fake_board.py --port 8080 --token AB9A9JQ2
"""

import argparse
import json
import pathlib
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlsplit

PART_BOUNDARY = "frameboundary"
VIEWER_PAGE = (
    b"<!doctype html><html><body style='background:#111;color:#eee'>"
    b"<h3>Fake face unlock board</h3><img id='live'>"
    b"<script>document.getElementById('live').src='/stream'+location.search;"
    b"</script></body></html>"
)
FAKE_DELAY_S = 3.0
EVENT_LOG_CAPACITY = 50

# Same ranges as the firmware (unlock_rules.h, camera_settings.h).
RULE_RANGES = {
    "min_similarity_percent": (30, 95),
    "confirm_frames": (1, 10),
    "unlock_hold_ms": (500, 60000),
    "cooldown_ms": (0, 60000),
    "enroll_timeout_ms": (1000, 120000),
    "approval_timeout_ms": (0, 120000),
}
CAMERA_RANGES = {
    "brightness": (-3, 3),
    "contrast": (-3, 3),
    "saturation": (-4, 4),
    "sharpness": (-3, 3),
    "exposure": (-5, 5),
    "denoise": (0, 8),
    "jpeg_quality": (10, 100),
}
CAMERA_SWITCHES = ("vflip", "hmirror")

frames = []
frame_delay_s = 0.1
token = ""
start_time_s = time.time()
lock = threading.Lock()
board = {
    "faces": [{"id": 1, "name": "Hao"}],
    "next_face_id": 2,
    "events": [],
    "last_seq": 0,
    "door_open": False,
    "enrolling": None,  # name while enrolling
    "waiting": None,  # {"request", "name", "time_ms"} while asking approval
    "next_request": 1000,
    "rules": {
        "min_similarity_percent": 55,
        "confirm_frames": 3,
        "unlock_hold_ms": 3000,
        "cooldown_ms": 2000,
        "enroll_timeout_ms": 15000,
        "approval_timeout_ms": 30000,
    },
    "camera": {
        "brightness": 0,
        "contrast": 0,
        "saturation": 0,
        "sharpness": 0,
        "exposure": 0,
        "denoise": 0,
        "vflip": False,
        "hmirror": False,
        "jpeg_quality": 80,
    },
}


def now_ms():
    return int((time.time() - start_time_s) * 1000)


def add_event(event_type, name="", face_id=0, request_id=0, similarity=0.0):
    """Caller holds the lock."""
    board["last_seq"] += 1
    board["events"].append(
        {
            "seq": board["last_seq"],
            "time_ms": now_ms(),
            "type": event_type,
            "face_id": face_id,
            "name": name,
            "similarity": similarity,
            "request_id": request_id,
        }
    )
    del board["events"][:-EVENT_LOG_CAPACITY]


def later(action):
    threading.Timer(FAKE_DELAY_S, action).start()


def open_door(name, face_id):
    """Caller holds the lock."""
    board["door_open"] = True
    add_event("door_opened", name, face_id)

    def close():
        with lock:
            board["door_open"] = False
            add_event("door_closed")

    later(close)


def state_name():
    if board["door_open"]:
        return "UNLOCKED"
    if board["enrolling"] is not None:
        return "ENROLLING"
    if board["waiting"] is not None:
        return "WAITING_APPROVAL"
    return "IDLE"


def merge(current, update, ranges, switches=()):
    """Copy of current with update applied, or None if a value is bad."""
    merged = dict(current)
    for key, value in update.items():
        if key in ranges:
            low, high = ranges[key]
            if not isinstance(value, int) or isinstance(value, bool):
                return None
            if not low <= value <= high:
                return None
        elif key in switches:
            if not isinstance(value, bool):
                return None
        else:
            continue  # unknown keys are ignored, like cJSON lookups
        merged[key] = value
    return merged


class BoardHandler(BaseHTTPRequestHandler):
    def has_token(self):
        header = self.headers.get("Authorization", "")
        if header.lower().startswith("bearer ") and header[7:] == token:
            return True
        query = parse_qs(urlsplit(self.path).query)
        return query.get("token", [""])[0] == token

    def send_body(self, status, content_type, body):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_json(self, status, value):
        self.send_body(status, "application/json", json.dumps(value).encode())

    def send_error_json(self, status, message):
        self.send_json(status, {"error": message})


class ApiHandler(BoardHandler):
    def read_json(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0 or length > 512:
            return None
        try:
            value = json.loads(self.rfile.read(length))
        except ValueError:
            return None
        return value if isinstance(value, dict) else None

    def route(self, method):
        path = urlsplit(self.path).path
        if not self.has_token():
            self.send_error_json(401, "missing or wrong token")
            return
        with lock:
            handler = ROUTES.get((method, path))
            if handler is None and method == "DELETE" and path.startswith("/api/faces/"):
                handler = ApiHandler.delete_face
            if handler is None:
                self.send_error_json(404, "not found")
            else:
                handler(self)

    def do_GET(self):
        self.route("GET")

    def do_POST(self):
        self.route("POST")

    def do_PUT(self):
        self.route("PUT")

    def do_DELETE(self):
        self.route("DELETE")

    def status(self):
        body = {
            "version": "fake",
            "recognition": True,
            "state": state_name(),
            "waiting_request": 0,
            "door": "open" if board["door_open"] else "locked",
            "faces": len(board["faces"]),
            "faces_max": 20,
            "ip": "127.0.0.1",
        }
        waiting = board["waiting"]
        if waiting is not None:
            left = board["rules"]["approval_timeout_ms"] - (now_ms() - waiting["time_ms"])
            body["waiting_request"] = waiting["request"]
            body["waiting_name"] = waiting["name"]
            body["waiting_left_ms"] = max(0, left)
        self.send_json(200, body)

    def events(self):
        query = parse_qs(urlsplit(self.path).query)
        after = int(query.get("after", ["0"])[0] or 0)
        self.send_json(
            200,
            {
                "now_ms": now_ms(),
                "last_seq": board["last_seq"],
                "events": [e for e in board["events"] if e["seq"] > after],
            },
        )

    def faces(self):
        self.send_json(200, {"max": 20, "faces": board["faces"]})

    def enroll(self):
        body = self.read_json()
        name = body.get("name") if body else None
        if not isinstance(name, str) or not 0 < len(name.encode()) <= 31:
            self.send_error_json(400, "value out of range")
            return
        if board["door_open"] or board["waiting"] is not None:
            # Firmware: the state machine answers BUSY with enroll_failed.
            add_event("enroll_failed", name)
            self.send_json(202, {"ok": True})
            return
        board["enrolling"] = name
        add_event("enroll_started", name)

        def finish():
            with lock:
                if board["enrolling"] != name:
                    return  # cancelled
                board["enrolling"] = None
                face_id = board["next_face_id"]
                board["next_face_id"] += 1
                board["faces"].append({"id": face_id, "name": name})
                add_event("face_enrolled", name, face_id)

        later(finish)
        self.send_json(202, {"ok": True})

    def enroll_cancel(self):
        if board["enrolling"] is not None:
            add_event("enroll_failed", board["enrolling"])
            board["enrolling"] = None
        self.send_json(202, {"ok": True})

    def delete_face(self):
        face_id_text = urlsplit(self.path).path[len("/api/faces/"):]
        if not face_id_text.isdigit() or not 0 < int(face_id_text) <= 0xFFFF:
            self.send_error_json(400, "not a face id")
            return
        before = len(board["faces"])
        board["faces"] = [f for f in board["faces"] if f["id"] != int(face_id_text)]
        if len(board["faces"]) == before:
            self.send_error_json(404, "no face with that id")
        else:
            self.send_json(202, {"ok": True})

    def unlock(self):
        open_door("manual", 0)
        self.send_json(202, {"ok": True})

    def approval(self):
        body = self.read_json() or {}
        request_id = body.get("request")
        approve = body.get("approve")
        if (
            not isinstance(request_id, int)
            or isinstance(request_id, bool)
            or request_id < 1
            or not isinstance(approve, bool)
        ):
            self.send_error_json(400, "value out of range")
            return
        waiting = board["waiting"]
        if waiting is not None and waiting["request"] == request_id:
            board["waiting"] = None
            if approve:
                open_door(waiting["name"], 1)
            else:
                add_event("approval_denied", waiting["name"], 1, request_id)
        self.send_json(202, {"ok": True})

    def visitor(self):
        request_id = board["next_request"]
        board["next_request"] += 1
        board["waiting"] = {"request": request_id, "name": "Hao", "time_ms": now_ms()}
        add_event("approval_requested", "Hao", 1, request_id, 0.71)

        def expire():
            with lock:
                waiting = board["waiting"]
                if waiting is not None and waiting["request"] == request_id:
                    board["waiting"] = None
                    add_event("approval_timeout", "Hao", 1, request_id)

        threading.Timer(board["rules"]["approval_timeout_ms"] / 1000, expire).start()
        self.send_json(202, {"ok": True, "request": request_id})

    def get_rules(self):
        self.send_json(200, board["rules"])

    def put_rules(self):
        body = self.read_json()
        merged = None if body is None else merge(board["rules"], body, RULE_RANGES)
        if merged is None:
            self.send_error_json(400, "value out of range")
            return
        board["rules"] = merged
        self.send_json(200, merged)

    def get_camera(self):
        self.send_json(200, board["camera"])

    def put_camera(self):
        body = self.read_json()
        merged = (
            None
            if body is None
            else merge(board["camera"], body, CAMERA_RANGES, CAMERA_SWITCHES)
        )
        if merged is None:
            self.send_error_json(400, "value out of range")
            return
        board["camera"] = merged
        self.send_json(200, merged)


ROUTES = {
    ("GET", "/api/status"): ApiHandler.status,
    ("GET", "/api/events"): ApiHandler.events,
    ("GET", "/api/faces"): ApiHandler.faces,
    ("POST", "/api/faces/enroll"): ApiHandler.enroll,
    ("POST", "/api/faces/enroll/cancel"): ApiHandler.enroll_cancel,
    ("POST", "/api/door/unlock"): ApiHandler.unlock,
    ("POST", "/api/approval"): ApiHandler.approval,
    ("POST", "/fake/visitor"): ApiHandler.visitor,
    ("GET", "/api/rules"): ApiHandler.get_rules,
    ("PUT", "/api/rules"): ApiHandler.put_rules,
    ("GET", "/api/camera"): ApiHandler.get_camera,
    ("PUT", "/api/camera"): ApiHandler.put_camera,
}


class StreamHandler(BoardHandler):
    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/":
            self.send_body(200, "text/html", VIEWER_PAGE)
        elif path not in ("/jpg", "/stream"):
            self.send_body(404, "text/plain", b"not found")
        elif not self.has_token():
            self.send_error_json(401, "missing or wrong token")
        elif path == "/jpg":
            frame = frames[int(time.time() / frame_delay_s) % len(frames)]
            self.send_body(200, "image/jpeg", frame)
        else:
            self.send_stream()

    def send_stream(self):
        try:
            self.send_response(200)
            self.send_header(
                "Content-Type", "multipart/x-mixed-replace;boundary=" + PART_BOUNDARY
            )
            self.end_headers()
            frame_index = 0
            while True:
                frame = frames[frame_index % len(frames)]
                part_header = (
                    f"\r\n--{PART_BOUNDARY}\r\n"
                    f"Content-Type: image/jpeg\r\nContent-Length: {len(frame)}\r\n\r\n"
                )
                self.wfile.write(part_header.encode("ascii"))
                self.wfile.write(frame)
                self.wfile.flush()
                frame_index += 1
                time.sleep(frame_delay_s)
        except (BrokenPipeError, ConnectionResetError):
            pass  # viewer closed the stream
        print("viewer gone")


def main():
    global frame_delay_s, token
    parser = argparse.ArgumentParser(description="Fake face-unlock board")
    parser.add_argument("--port", type=int, default=8080, help="API port; stream is +1")
    parser.add_argument("--token", default="AB9A9JQ2")
    parser.add_argument("--fps", type=int, default=10)
    parser.add_argument(
        "--frames",
        type=pathlib.Path,
        default=pathlib.Path(__file__).parent / "fake_frames",
        help="folder of .jpg files to play in a loop",
    )
    args = parser.parse_args()

    for path in sorted(args.frames.glob("*.jpg")):
        frames.append(path.read_bytes())
    if not frames:
        parser.error(f"no .jpg files in {args.frames}")
    frame_delay_s = 1.0 / args.fps
    token = args.token

    # Not ThreadingHTTPServer: each board server answers one request at a time.
    api_server = HTTPServer(("0.0.0.0", args.port), ApiHandler)
    stream_server = HTTPServer(("0.0.0.0", args.port + 1), StreamHandler)
    threading.Thread(target=api_server.serve_forever, daemon=True).start()
    print(
        f"fake board: API http://0.0.0.0:{args.port}/api/status, "
        f"stream http://0.0.0.0:{args.port + 1}/?token={token} "
        f"({len(frames)} frames)"
    )
    stream_server.serve_forever()


if __name__ == "__main__":
    main()
