#!/usr/bin/env python3
"""Fake serial console of the face-unlock board, on a pty.

Behaves like the firmware's esp_console in dumb mode (see
docs/console_protocol.md): every byte is echoed, the prompt is "face> ",
replies and log lines end with CRLF, and random face events, telemetry and
face boxes appear in the log like on a real board. Standard library only.

  python3 tool/fake_console.py --pty-link /tmp/face_unlock_tty
  then open /tmp/face_unlock_tty with any serial program (baud is ignored).

Extra command on the fake only: "panic" prints a Guru Meditation block and
reboots, so a log decoder can be tested without a crash on real hardware.
"""

import argparse
import json
import os
import random
import threading
import time
import tty

PROMPT = "face> "
FRAME_WIDTH = 320
FRAME_HEIGHT = 240
FACE_CAPACITY = 20
NAME_MAX_LEN = 31
NAME_CHARS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")
MIN_SIMILARITY = 0.55
STATS_MIN_PERIOD_MS = 100
BOX_PERIOD_S = 0.1
UNLOCK_HOLD_S = 3.0
COOLDOWN_S = 2.0
ENROLL_LOOK_DELAY_S = 1.0
ENROLL_DONE_DELAY_S = 3.0
APPROVAL_TIMEOUT_S = 30.0
EVENT_GAP_MIN_S = 5.0
EVENT_GAP_MAX_S = 20.0
FACE_SEEN_MIN_S = 2.0
FACE_SEEN_MAX_S = 4.0
TICK_S = 0.05
APP_VERSION = "fcb63ff-dirty"
IDF_VERSION = "v5.5.1"

STATE_NAMES = ["idle", "confirming", "unlocked", "cooldown", "enrolling", "waiting approval"]

# (command, hint, help) in the firmware's registration order, "help" first
# because esp_console registers it itself.
COMMAND_HELP = [
    ("help", "[<string>]", "Print the summary of all registered commands if no arguments "
     "are given,\n  otherwise print summary of given command.\n      <string>  Name of command"),
    ("status", "[json]", "Show state, last match, faces and memory"),
    ("list", "[json]", "List enrolled faces"),
    ("enroll", "<name>", "Enroll the next face seen: enroll <name>"),
    ("cancel", "", "Cancel a running enroll"),
    ("delete", "<id>", "Delete one face: delete <id>"),
    ("clear", "yes", "Delete all faces: clear yes"),
    ("unlock", "", "Open the door now (manual override)"),
    ("approve", "<id>", "Approve a face waiting for approval: approve <id>"),
    ("deny", "<id>", "Deny a face waiting for approval: deny <id>"),
    ("stats", "<period_ms>", "Print telemetry lines every <period_ms> ms (0 = off)"),
    ("free", "", "Show free heap: internal and psram"),
    ("restart", "", "Reboot the board"),
    ("log", "<tag> <level>", "Set the log level of one tag (* = all)"),
    ("tasks", "", "List FreeRTOS tasks"),
]

LOG_LEVELS = ["none", "error", "warn", "info", "debug", "verbose"]

# Lines before the app starts are a guess at what the ROM and bootloader
# print; no real boot log exists yet. The tool must only rely on the app lines.
ROM_BANNER = [
    "ESP-ROM:esp32s3-20210327",
    "Build:Mar 27 2021",
    "rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)",
    "SPIWP:0xee",
    "mode:DIO, clock div:1",
    "load:0x3fce2820,len:0x15a0",
    "load:0x403c8700,len:0x4",
    "load:0x403c8704,len:0xd20",
    "load:0x403cb700,len:0x2f10",
    "entry 0x403c8928",
]

TASK_TABLE = [
    ("face_logic", "R", 5, 2048, 12, 1),
    ("console", "R", 2, 1500, 11, 0),
    ("httpd", "B", 5, 3100, 13, 0),
    ("wifi", "B", 23, 2400, 9, 0),
    ("tiT", "B", 18, 2900, 10, 0),
    ("esp_timer", "S", 22, 3400, 3, 0),
    ("ipc0", "S", 24, 700, 1, 0),
    ("ipc1", "S", 24, 700, 2, 1),
    ("IDLE0", "R", 0, 900, 4, 0),
    ("IDLE1", "R", 0, 900, 5, 1),
]


class FakeConsole:
    def __init__(self, seed=None, use_approval=False, elf_sha256=None, pty_link=None,
                 no_events=False):
        self.random = random.Random(seed)
        self.use_approval = use_approval
        self.no_events = no_events
        self.elf_sha256 = elf_sha256 or "1a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f809"
        self.pty_link = pty_link
        self.write_lock = threading.Lock()
        self.state_lock = threading.Lock()
        self.master_fd = None
        self.slave_fd = None
        self.pty_path = None
        self.log_levels = {}
        self.reset_board_state()

    # ---- board state ------------------------------------------------------

    def reset_board_state(self):
        now = time.monotonic()
        self.boot_time = now
        self.faces = [{"id": 1, "name": "alice"}, {"id": 2, "name": "bob"}]
        self.next_face_id = 3
        self.state = "idle"
        self.state_until = 0.0
        self.door_open_until = 0.0
        self.enroll_name = None
        self.enroll_look_at = 0.0
        self.enroll_done_at = 0.0
        self.waiting_request_id = 0
        self.waiting_face = None
        self.approval_expires_at = 0.0
        self.next_request_id = self.random.randrange(1000, 90000)
        self.frames = 0
        self.frames_with_face = 0
        self.inference_ms = 85
        self.last_face_id = 0
        self.last_similarity = 0.0
        self.stats_period_ms = 0
        self.stats_last_at = now
        self.stats_frames = 0
        self.face_seen_until = 0.0
        self.face_seen_id = 0
        self.face_seen_similarity = 0.0
        self.box = [112, 40, 96, 120]
        self.box_last_at = 0.0
        self.next_event_at = now + self.random.uniform(EVENT_GAP_MIN_S, EVENT_GAP_MAX_S)
        self.pending_event = None
        self.pending_event_at = 0.0
        self.next_noise_at = now + self.random.uniform(10.0, 40.0)

    def uptime_ms(self):
        return int((time.monotonic() - self.boot_time) * 1000)

    def heap_internal_free(self):
        return 180000 - (self.uptime_ms() // 1000) % 7000 + self.random.randrange(0, 512)

    def heap_psram_free(self):
        return 6900000 + self.random.randrange(0, 4096)

    # ---- pty and writing --------------------------------------------------

    def start(self):
        self.master_fd, self.slave_fd = os.openpty()
        tty.setraw(self.slave_fd)
        self.pty_path = os.ttyname(self.slave_fd)
        if self.pty_link:
            if os.path.islink(self.pty_link) or os.path.exists(self.pty_link):
                os.remove(self.pty_link)
            os.symlink(self.pty_path, self.pty_link)
            print(f"fake console on {self.pty_path}  (link {self.pty_link})", flush=True)
        else:
            print(f"fake console on {self.pty_path}", flush=True)
        self.print_boot_banner()
        threading.Thread(target=self.reader_loop, name="console-reader", daemon=True).start()
        threading.Thread(target=self.ticker_loop, name="console-ticker", daemon=True).start()

    def stop(self):
        if self.pty_link and os.path.islink(self.pty_link):
            os.remove(self.pty_link)

    def write_raw(self, data):
        with self.write_lock:
            try:
                os.write(self.master_fd, data)
            except OSError:
                pass  # nobody reading; keep the board "running"

    def write_line(self, text):
        self.write_raw((text + "\r\n").encode("utf-8", "replace"))

    def write_prompt(self):
        self.write_raw(PROMPT.encode("ascii"))

    def log(self, level, tag, text):
        self.write_line(f"{level} ({self.uptime_ms()}) {tag}: {text}")

    def publish(self, event_name, face_id=0, name="", similarity=0.0, request_id=0):
        payload = (f'{{"id":{face_id},"name":"{name}","similarity":{similarity:.3f},'
                   f'"request":{request_id}}}')
        self.log("I", "net_comm", f"publish face_unlock/{event_name} {payload}")

    # ---- boot and reboot --------------------------------------------------

    def print_boot_banner(self):
        for line in ROM_BANNER:
            self.write_line(line)
        self.write_line(f"I (30) boot: ESP-IDF {IDF_VERSION} 2nd stage bootloader")
        self.write_line(f"I (31) boot: compile time Nov  4 2026 16:42:10")
        self.write_line("I (120) boot: Loaded app from partition at offset 0x10000")
        self.write_line(f"I (150) esp_image: App version:      {APP_VERSION}")
        self.write_line(f"I (155) esp_image: ELF file SHA256:  {self.elf_sha256[:16]}...")
        self.write_line("I (280) cpu_start: Pro cpu start user code")
        self.log("I", "app_main", f"Face unlock {APP_VERSION} (IDF {IDF_VERSION})")
        self.log("I", "door_control", "Lock on GPIO 2, active high")
        self.log("I", "net_comm", "Joining Wi-Fi 'fake'")
        self.log("I", "ai_face", "Models loaded in 420 ms, embedding length 512")
        self.log("I", "face_db_nvs", f"Loaded {len(self.faces)} face(s), skipped 0, capacity {FACE_CAPACITY}")
        self.log("I", "camera_driver", "Sensor PID 0x5640")
        self.log("I", "camera_driver", "Camera ready")
        self.log("I", "net_comm", "Wi-Fi connected, IP 127.0.0.1")
        self.log("I", "system_logic", f"Recognition running ({len(self.faces)} faces enrolled)")
        self.log("I", "video_stream", "Live view on http://127.0.0.1/ (port 80)")
        self.log("I", "app_main", "Ready. Type 'help' in the console.")
        self.write_line("")
        self.write_line("Type 'help' to get the list of commands.")
        self.write_line("Use UP/DOWN arrows to navigate through command history.")
        self.write_line("Press TAB when typing command name to auto-complete.")
        self.write_line("Your terminal application does not support escape sequences.")
        self.write_line("Line editing and history features are disabled.")
        self.write_line("On Windows, try using Windows Terminal or Putty instead.")
        self.write_prompt()

    def reboot(self):
        with self.state_lock:
            self.reset_board_state()
        time.sleep(0.3)
        self.print_boot_banner()

    # ---- reader: echo and command lines ----------------------------------

    def reader_loop(self):
        line = bytearray()
        while True:
            try:
                chunk = os.read(self.master_fd, 256)
            except OSError:
                time.sleep(0.1)
                continue
            for byte in chunk:
                self.take_byte(byte, line)

    def take_byte(self, byte, line):
        if byte in (0x0A, 0x0D):
            self.write_raw(b"\r\n")
            self.run_line(line.decode("utf-8", "replace"))
            line.clear()
            self.write_prompt()
        elif byte in (0x08, 0x7F):
            if line:
                line.pop()
                self.write_raw(b"\x08 \x08")
        elif byte == 0x03:
            line.clear()
            self.write_raw(b"\r\n")
            self.write_prompt()
        else:
            line.append(byte)
            self.write_raw(bytes([byte]))

    def run_line(self, text):
        words = text.split()
        if not words:
            return
        handler = getattr(self, "command_" + words[0], None)
        if handler is None or words[0] in ("command_", "run_line"):
            self.write_line("Unrecognized command")
            return
        with self.state_lock:
            status = handler(words)
        if status != 0:
            self.write_line("Command returned non-zero error code: 0x1 (ESP_FAIL)")

    # ---- commands (reply texts copied from console_commands.c) -----------

    def command_help(self, words):
        wanted = words[1] if len(words) > 1 else None
        for command, hint, help_text in COMMAND_HELP:
            if wanted and command != wanted:
                continue
            self.write_line(f"{command} {hint}".rstrip() + " ")
            self.write_line(f"  {help_text}")
            self.write_line("")
        return 0

    def command_status(self, words):
        if len(words) == 2 and words[1] == "json":
            self.write_line("@status " + json.dumps(self.status_as_dict(), separators=(",", ":")))
            return 0
        if len(words) != 1:
            self.write_line("Usage: status [json]")
            return 1
        self.write_line(f"state            {self.state}")
        self.write_line(f"frames           {self.frames} (with face {self.frames_with_face})")
        self.write_line(f"inference        {self.inference_ms} ms")
        self.write_line(f"last best match  id {self.last_face_id}, similarity "
                        f"{self.last_similarity:.3f} (need {MIN_SIMILARITY:.2f})")
        if self.waiting_request_id:
            self.write_line(f"approval         waiting #{self.waiting_request_id} "
                            f"(approve / deny {self.waiting_request_id})")
        self.write_line(f"door             {'open' if self.is_door_open() else 'locked'}")
        self.write_line("wifi             connected, live view http://127.0.0.1/")
        self.write_line(f"faces            {len(self.faces)} / {FACE_CAPACITY} (skipped at boot: 0)")
        self.write_line(f"free heap        internal {self.heap_internal_free()}, "
                        f"psram {self.heap_psram_free()}")
        return 0

    def status_as_dict(self):
        approval = {"request_id": self.waiting_request_id} if self.waiting_request_id else None
        return {
            "state": self.state,
            "state_id": STATE_NAMES.index(self.state),
            "frames": self.frames,
            "frames_with_face": self.frames_with_face,
            "inference_ms": self.inference_ms,
            "last_match": {"id": self.last_face_id,
                           "similarity": round(self.last_similarity, 3),
                           "threshold": MIN_SIMILARITY},
            "approval": approval,
            "door": "open" if self.is_door_open() else "locked",
            "wifi": {"enabled": True, "connected": True, "ip": "127.0.0.1"},
            "faces": {"count": len(self.faces), "capacity": FACE_CAPACITY, "skipped": 0},
            "heap": {"internal": self.heap_internal_free(),
                     "psram": self.heap_psram_free(),
                     "internal_min": 110000},
            "version": {"app": APP_VERSION, "idf": IDF_VERSION,
                        "compile_date": "Nov  4 2026", "compile_time": "16:42:10",
                        "elf_sha256_short": self.elf_sha256[:8]},
            "uptime_ms": self.uptime_ms(),
        }

    def command_list(self, words):
        if len(words) == 2 and words[1] == "json":
            self.write_line("@list " + json.dumps(self.faces, separators=(",", ":")))
            return 0
        if not self.faces:
            self.write_line("No faces enrolled. Use: enroll <name>")
            return 0
        self.write_line("  id  name")
        for face in self.faces:
            self.write_line(f"{face['id']:4d}  {face['name']}")
        return 0

    def command_enroll(self, words):
        if len(words) != 2:
            self.write_line("Usage: enroll <name>")
            return 1
        name = words[1]
        if len(name) > NAME_MAX_LEN:
            self.write_line(f"Name too long (max {NAME_MAX_LEN} characters)")
            return 1
        if not set(name) <= NAME_CHARS:
            self.write_line("Name: letters, digits, _ and - only")
            return 1
        if self.state in ("unlocked", "waiting approval"):
            self.log("W", "system_logic", "Request refused: door open or waiting for approval")
            self.write_line("Look at the camera. Watch the log for the result.")
            return 0
        now = time.monotonic()
        self.enroll_name = name
        self.enroll_look_at = now + ENROLL_LOOK_DELAY_S
        self.enroll_done_at = now + ENROLL_DONE_DELAY_S
        self.write_line("Look at the camera. Watch the log for the result.")
        return 0

    def command_cancel(self, words):
        if self.enroll_name:
            self.log("I", "system_logic", f"Enroll '{self.enroll_name}': enroll cancelled")
            self.publish("enroll_failed", name=self.enroll_name)
            self.enroll_name = None
            self.set_state("idle")
        return 0

    def parse_id(self, text, usage):
        if not text.isdigit() or int(text) == 0 or int(text) > 65535:
            self.write_line(f"Not a valid id: {text}")
            return None
        return int(text)

    def command_delete(self, words):
        if len(words) != 2:
            self.write_line("Usage: delete <id>   (ids from 'list')")
            return 1
        face_id = self.parse_id(words[1], None)
        if face_id is None:
            return 1
        for face in self.faces:
            if face["id"] == face_id:
                self.faces.remove(face)
                self.write_line("Deleted")
                return 0
        self.write_line("face not found")
        return 1

    def command_clear(self, words):
        if len(words) != 2 or words[1] != "yes":
            self.write_line("This deletes every enrolled face. Type: clear yes")
            return 1
        self.faces.clear()
        self.write_line("All faces deleted")
        return 0

    def command_unlock(self, words):
        if self.state in ("unlocked", "waiting approval"):
            self.log("W", "system_logic", "Request refused: door open or waiting for approval")
            return 0
        if self.enroll_name:
            self.log("I", "system_logic", f"Enroll '{self.enroll_name}' ended by manual unlock")
            self.enroll_name = None
        self.open_door("manual", 0, 0.0)
        return 0

    def command_approve(self, words):
        return self.answer_approval(words, True)

    def command_deny(self, words):
        return self.answer_approval(words, False)

    def answer_approval(self, words, is_approve):
        if len(words) != 2:
            self.write_line(f"Usage: {words[0]} <id>   (id from the log or 'status')")
            return 1
        if not words[1].isdigit() or int(words[1]) == 0:
            self.write_line(f"Not a valid id: {words[1]}")
            return 1
        request_id = int(words[1])
        if request_id != self.waiting_request_id:
            self.log("W", "system_logic", f"Answer to approval #{request_id} ignored: not waiting for it")
            return 0
        face = self.waiting_face
        self.waiting_request_id = 0
        self.waiting_face = None
        if is_approve:
            self.open_door(face["name"], face["id"], face["similarity"])
        else:
            self.log("I", "system_logic", f"REJECT '{face['name']}': approval #{request_id} denied")
            self.publish("approval_denied", face["id"], face["name"], face["similarity"], request_id)
            self.set_state("cooldown", COOLDOWN_S)
        return 0

    def command_stats(self, words):
        if len(words) != 2 or not words[1].isdigit():
            self.write_line("Usage: stats <period_ms>   (0 = off)")
            return 1
        period_ms = int(words[1])
        if period_ms == 0:
            self.stats_period_ms = 0
            self.write_line("Telemetry off")
            return 0
        self.stats_period_ms = max(period_ms, STATS_MIN_PERIOD_MS)
        self.stats_last_at = time.monotonic()
        self.stats_frames = 0
        self.write_line(f"Telemetry every {self.stats_period_ms} ms")
        return 0

    def command_free(self, words):
        internal = self.heap_internal_free()
        psram = self.heap_psram_free()
        self.write_line(f"internal free {internal}, min 110000, largest {internal - 20000}")
        self.write_line(f"psram free {psram}, min 6800000, largest {psram - 100000}")
        return 0

    def command_restart(self, words):
        self.write_line("Restarting")
        threading.Thread(target=self.reboot, daemon=True).start()
        return 0

    def command_log(self, words):
        if len(words) != 3 or words[2] not in LOG_LEVELS:
            self.write_line("Usage: log <tag> <level>   (none|error|warn|info|debug|verbose)")
            return 1
        self.log_levels[words[1]] = words[2]
        self.write_line(f"Log level of '{words[1]}' set to {words[2]}")
        return 0

    def command_tasks(self, words):
        self.write_line("name            state prio stack num core")
        for name, state, priority, stack_free, number, core in TASK_TABLE:
            self.write_line(f"{name:<16}{state}     {priority:<5}{stack_free:<6}{number:<4}{core}")
        return 0

    def command_panic(self, words):
        threading.Thread(target=self.print_panic_and_reboot, daemon=True).start()
        return 0

    def print_panic_and_reboot(self):
        time.sleep(0.1)
        self.write_line("Guru Meditation Error: Core  1 panic'ed (LoadProhibited). Exception was unhandled.")
        self.write_line("")
        self.write_line("Core  1 register dump:")
        self.write_line("PC      : 0x4200abcd  PS      : 0x00060030  A0      : 0x82001234  A1      : 0x3fcebbd0")
        self.write_line("A2      : 0x00000000  A3      : 0x3fc9a4f0  A4      : 0x00000001  A5      : 0x3fcebc00")
        self.write_line("EXCCAUSE: 0x0000001c  EXCVADDR: 0x00000000  LBEG    : 0x40056f5c  LEND    : 0x40056f72")
        self.write_line("")
        self.write_line("Backtrace: 0x4200abcd:0x3fcebbd0 0x42001234:0x3fcebbf0 0x42002345:0x3fcebc20 0x4037a1b0:0x3fcebc50")
        self.write_line("")
        self.write_line("")
        self.write_line(f"ELF file SHA256: {self.elf_sha256}")
        self.write_line("")
        self.write_line("Rebooting...")
        self.reboot()

    # ---- door and state ---------------------------------------------------

    def is_door_open(self):
        return time.monotonic() < self.door_open_until

    def set_state(self, state, hold_s=0.0):
        self.state = state
        self.state_until = time.monotonic() + hold_s if hold_s else 0.0

    def open_door(self, name, face_id, similarity):
        self.log("I", "system_logic", f"OPEN for '{name}' (id {face_id}, similarity {similarity:.2f})")
        self.log("I", "door_control", f"Unlocked for {int(UNLOCK_HOLD_S * 1000)} ms")
        self.publish("door_opened", face_id, name, similarity)
        self.door_open_until = time.monotonic() + UNLOCK_HOLD_S
        self.set_state("unlocked", UNLOCK_HOLD_S)

    def close_door(self):
        self.log("I", "door_control", "Locked (hold time over)")
        self.publish("door_closed")
        self.set_state("cooldown", COOLDOWN_S)

    # ---- ticker: timers, random events, telemetry ------------------------

    def ticker_loop(self):
        while True:
            time.sleep(TICK_S)
            with self.state_lock:
                self.tick(time.monotonic())

    def tick(self, now):
        self.count_frame(now)
        self.tick_timers(now)
        self.tick_events(now)
        self.tick_box(now)
        self.tick_stats(now)
        self.tick_noise(now)

    def count_frame(self, now):
        # about 10 fps: one frame every other tick
        if int(now / TICK_S) % 2 == 0:
            self.frames += 1
            self.stats_frames += 1
            self.inference_ms = 80 + self.random.randrange(0, 12)
            if now < self.face_seen_until:
                self.frames_with_face += 1

    def tick_timers(self, now):
        if self.state == "unlocked" and now >= self.door_open_until:
            self.close_door()
        elif self.state == "cooldown" and now >= self.state_until:
            self.set_state("idle")
        if self.enroll_name and self.enroll_look_at and now >= self.enroll_look_at:
            self.enroll_look_at = 0.0
            self.log("I", "system_logic", f"Enrolling '{self.enroll_name}': look at the camera")
            self.publish("enroll_started", name=self.enroll_name)
            self.set_state("enrolling")
            self.see_face(now, 0, 0.0)
        if self.enroll_name and now >= self.enroll_done_at:
            self.finish_enroll()
        if self.waiting_request_id and now >= self.approval_expires_at:
            face = self.waiting_face
            self.log("I", "system_logic", f"REJECT '{face['name']}': approval #{self.waiting_request_id} timed out")
            self.publish("approval_timeout", face["id"], face["name"], face["similarity"], self.waiting_request_id)
            self.waiting_request_id = 0
            self.waiting_face = None
            self.set_state("cooldown", COOLDOWN_S)

    def finish_enroll(self):
        name = self.enroll_name
        self.enroll_name = None
        if len(self.faces) >= FACE_CAPACITY:
            self.log("E", "system_logic", f"Enroll '{name}' failed: face store full")
            self.publish("enroll_failed", name=name)
        else:
            face_id = self.next_face_id
            self.next_face_id += 1
            self.faces.append({"id": face_id, "name": name})
            self.log("I", "system_logic", f"Enrolled '{name}' as id {face_id} ({len(self.faces)} faces)")
            self.publish("face_enrolled", face_id, name, 1.0)
        self.set_state("cooldown", COOLDOWN_S)

    def see_face(self, now, face_id, similarity):
        self.face_seen_until = now + self.random.uniform(FACE_SEEN_MIN_S, FACE_SEEN_MAX_S)
        self.face_seen_id = face_id
        self.face_seen_similarity = similarity
        self.box = [self.random.randrange(60, 160), self.random.randrange(20, 80), 96, 120]
        self.last_face_id = face_id
        self.last_similarity = similarity

    def tick_events(self, now):
        if self.pending_event and now >= self.pending_event_at:
            event = self.pending_event
            self.pending_event = None
            event()
        if self.no_events or now < self.next_event_at or self.state != "idle" or self.enroll_name:
            return
        self.next_event_at = now + self.random.uniform(EVENT_GAP_MIN_S, EVENT_GAP_MAX_S)
        self.start_random_event(now)

    def start_random_event(self, now):
        known_possible = bool(self.faces)
        roll = self.random.random()
        if known_possible and roll < 0.6:
            face = self.random.choice(self.faces)
            similarity = self.random.uniform(0.60, 0.85)
            self.see_face(now, face["id"], similarity)
            self.set_state("confirming")
            if self.use_approval:
                self.pending_event = lambda: self.ask_approval(face, similarity)
            else:
                self.pending_event = lambda: self.open_door(face["name"], face["id"], similarity)
        else:
            similarity = self.random.uniform(0.20, 0.50)
            self.see_face(now, 0, similarity)
            self.set_state("confirming")
            self.pending_event = lambda: self.reject_unknown(similarity)
        self.pending_event_at = now + 0.4

    def reject_unknown(self, similarity):
        self.log("I", "system_logic", f"REJECT unknown face (best similarity {similarity:.2f})")
        self.publish("face_rejected", 0, "", similarity)
        self.set_state("cooldown", COOLDOWN_S)

    def ask_approval(self, face, similarity):
        request_id = self.next_request_id
        self.next_request_id += 1
        self.waiting_request_id = request_id
        self.waiting_face = {"id": face["id"], "name": face["name"], "similarity": similarity}
        self.approval_expires_at = time.monotonic() + APPROVAL_TIMEOUT_S
        self.log("I", "system_logic", f"WAIT for approval #{request_id} for '{face['name']}' "
                 f"(similarity {similarity:.2f}): approve {request_id} / deny {request_id}")
        self.publish("approval_requested", face["id"], face["name"], similarity, request_id)
        self.set_state("waiting approval")

    def tick_box(self, now):
        if self.stats_period_ms == 0 or now >= self.face_seen_until:
            return
        if now - self.box_last_at < BOX_PERIOD_S:
            return
        self.box_last_at = now
        self.box[0] = min(max(self.box[0] + self.random.randrange(-3, 4), 0), FRAME_WIDTH - self.box[2])
        self.box[1] = min(max(self.box[1] + self.random.randrange(-3, 4), 0), FRAME_HEIGHT - self.box[3])
        box_x, box_y, box_w, box_h = self.box
        self.write_line(f">box:{box_x},{box_y},{box_w},{box_h},{self.face_seen_id},"
                        f"{self.face_seen_similarity:.3f}")

    def tick_stats(self, now):
        if self.stats_period_ms == 0:
            return
        elapsed_s = now - self.stats_last_at
        if elapsed_s * 1000 < self.stats_period_ms:
            return
        fps = self.stats_frames / elapsed_s if elapsed_s > 0 else 0.0
        self.stats_last_at = now
        self.stats_frames = 0
        rssi = -55 - self.random.randrange(0, 12)
        self.write_line(f">fps:{fps:.1f},infer_ms:{self.inference_ms},heap_free:{self.heap_internal_free()},"
                        f"psram_free:{self.heap_psram_free()},rssi:{rssi},"
                        f"state:{STATE_NAMES.index(self.state)},uptime_s:{self.uptime_ms() // 1000}")

    def tick_noise(self, now):
        if now < self.next_noise_at:
            return
        self.next_noise_at = now + self.random.uniform(10.0, 40.0)
        noise = self.random.choice([
            ("W", "wifi", "<ba-add>idx:0 (ifx:0, 11:22:33:44:55:66), tid:0, ssn:0, winSize:64"),
            ("I", "wifi", "bcn_timout,ap_probe_send_start"),
            ("W", "video_stream", "Viewer gone (ESP_ERR_HTTPD_RESP_SEND)"),
            ("I", "video_stream", "Viewer connected"),
        ])
        self.log(*noise)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pty-link", help="symlink to create for the pty, e.g. /tmp/face_unlock_tty")
    parser.add_argument("--seed", type=int, help="random seed for repeatable events")
    parser.add_argument("--approval", action="store_true", help="known faces wait for approve/deny")
    parser.add_argument("--elf-sha256", help="hash printed by 'panic' (64 hex chars)")
    parser.add_argument("--no-events", action="store_true", help="no random face events (for tests)")
    args = parser.parse_args()
    console = FakeConsole(args.seed, args.approval, args.elf_sha256, args.pty_link, args.no_events)
    console.start()
    try:
        while True:
            time.sleep(1.0)
    except KeyboardInterrupt:
        pass
    finally:
        console.stop()


if __name__ == "__main__":
    main()
