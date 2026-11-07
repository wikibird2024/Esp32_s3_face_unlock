#!/usr/bin/env python3
"""Fake face-unlock board: serves the same HTTP endpoints as the firmware,
so the app can be tested without hardware.

  port N    (firmware: board_api, port 80)
    /api/status  JSON status
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
UNAUTHORIZED = b'{"error":"missing or wrong token"}'

frames = []
frame_delay_s = 0.1
token = ""


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

    def send_unauthorized(self):
        self.send_body(401, "application/json", UNAUTHORIZED)


class ApiHandler(BoardHandler):
    def do_GET(self):
        path = urlsplit(self.path).path
        if path != "/api/status":
            self.send_body(404, "text/plain", b"not found")
        elif not self.has_token():
            self.send_unauthorized()
        else:
            status = {
                "version": "fake",
                "recognition": True,
                "state": "IDLE",
                "waiting_request": 0,
                "door": "locked",
                "faces": 1,
                "faces_max": 20,
                "ip": self.server.server_address[0],
            }
            self.send_body(200, "application/json", json.dumps(status).encode())


class StreamHandler(BoardHandler):
    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/":
            self.send_body(200, "text/html", VIEWER_PAGE)
        elif path not in ("/jpg", "/stream"):
            self.send_body(404, "text/plain", b"not found")
        elif not self.has_token():
            self.send_unauthorized()
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
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
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
    print(f"fake board: API http://0.0.0.0:{args.port}/api/status, "
          f"stream http://0.0.0.0:{args.port + 1}/?token={token} "
          f"({len(frames)} frames)")
    stream_server.serve_forever()


if __name__ == "__main__":
    main()
