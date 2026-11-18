# Console protocol: firmware ⇄ PC bench tool

Author: Hao Tran. Protocol version: 1.1 (1.1 adds the `door_fault` event line; older tools ignore it).

This file is the contract between the firmware console (`console_commands`,
`system_logic`) and `pc_tool/` (the desktop bench app) and the fake board
(`mobile_app/tool/fake_console.py`). Change it here first, then both sides.

## 1. Transport

| Item | Value |
|---|---|
| Port | UART0 bridge (CP210x / CH340 / FTDI). The USB-Serial-JTAG port only mirrors logs; it cannot take commands. |
| Speed | 115200 8N1 |
| Line ending (board → PC) | `\r\n` (`CONFIG_NEWLIB_STDOUT_LINE_ENDING_CRLF`) |
| Line ending (PC → board) | `\n` |
| Prompt | `face> ` (with trailing space, no newline). Colour wrapped (`\x1b[0;32m ... \x1b[0m`) only if the PC answered the terminal probe; the tool does not, so expect plain text. |
| Echo | The console is in dumb mode: every byte the PC sends is echoed back. The first reply line therefore equals the command text. |
| Log colours | Off (`CONFIG_LOG_COLORS` not set). Strip `\x1b[ ... m` anyway. |

A reply is finished when the receive buffer ends with `face> `. Log lines from
other tasks can interleave with a reply; the tool must classify each line
(section 7) and ignore log lines inside a reply.

Error lines after a command:

```
Unrecognized command
Command returned non-zero error code: 0x1 (ESP_FAIL)
Failed: <esp_err name>
Usage: <text>
Face store not available (see boot log).
```

## 2. Commands

| Command | Reply |
|---|---|
| `help` | ESP-IDF help text, one block per command |
| `status` | Column text (unchanged, for humans) |
| `status json` | One line `@status {...}` (section 3). Any other argument → `Usage: status [json]` |
| `list` | Column text (unchanged) |
| `list json` | One line `@list [...]` (section 4). Any other argument → `Usage: list [json]` |
| `enroll <name>` | `Look at the camera. Watch the log for the result.` Name: 1-31 chars from `[A-Za-z0-9_-]`, else `Name: letters, digits, _ and - only` |
| `cancel` | nothing on success |
| `delete <id>` | `Deleted` or a face-store status name |
| `clear yes` | `All faces deleted` |
| `unlock` | nothing on success; result in the log |
| `approve <id>` / `deny <id>` | nothing on success |
| `stats <period_ms>` | `Telemetry every <n> ms` or `Telemetry off`. 0 = off, minimum 100 (smaller values are raised to 100). Default off at boot. Bad number → `Not a valid period: <text>` |
| `free` | `internal free <n>, min <n>, largest <n>` and `psram free <n>, min <n>, largest <n>` |
| `restart` | `Restarting` then reboot |
| `log <tag> <level>` | `Log level of '<tag>' set to <level>`; level ∈ none, error, warn, info, debug, verbose; tag `*` = all |
| `tasks` | Header `name            state prio stack num core`, then one vTaskList line per task. Only present when `CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS` is on. |

## 3. `@status` line

```
@status {"state":"idle","state_id":0,"frames":1234,"frames_with_face":56,"inference_ms":85,"last_match":{"id":1,"similarity":0.712,"threshold":0.55},"approval":null,"door":"locked","wifi":{"enabled":true,"connected":true,"ip":"192.168.1.20"},"faces":{"count":2,"capacity":20,"skipped":0},"heap":{"internal":123456,"psram":7000000,"internal_min":110000},"version":{"app":"fcb63ff-dirty","idf":"v5.5.1","compile_date":"Nov  4 2026","compile_time":"16:42:10","elf_sha256_short":"1a2b3c4d"},"uptime_ms":123456}
```

| Key | Meaning |
|---|---|
| `state` | `unlock_state_name()`: `idle`, `confirming`, `unlocked`, `cooldown`, `enrolling`, `waiting approval` (has a space) |
| `state_id` | `unlock_state_t` number, same order as above (0..5) |
| `frames`, `frames_with_face` | counters since boot |
| `inference_ms` | last inference time |
| `last_match` | `id` 0 = none; `similarity` 3 decimals; `threshold` 2 decimals |
| `approval` | `null` or `{"request_id":1234}` while waiting |
| `door` | `"open"` or `"locked"` |
| `wifi` | `enabled` = SSID set at build time; `ip` is `null` when not connected |
| `faces` | `null` when the face store failed to open |
| `heap` | bytes: free internal, free PSRAM, minimum internal ever |
| `version.app` | `git describe --always --tags --dirty` at configure time (ESP-IDF `PROJECT_VER`) |
| `version.elf_sha256_short` | first 8 hex chars of the running ELF's SHA-256 (same hash the panic handler prints and `sha256sum build/face_unlock_esp32_s3.elf` gives) |
| `uptime_ms` | `esp_timer_get_time() / 1000` |

Strings escape `"` and `\` with a backslash; control bytes become `?`.

## 4. `@list` line

```
@list [{"id":1,"name":"alice"},{"id":2,"name":"bob"}]
```

`[]` when nothing is enrolled.

## 5. Telemetry lines (`stats <period_ms>` on)

Printed by the `face_logic` task, one `printf` per line:

```
>fps:9.8,infer_ms:85,heap_free:123456,psram_free:7000000,rssi:-60,state:2,uptime_s:123
```

| Key | Meaning |
|---|---|
| `fps` | frames in the period / period, one decimal |
| `infer_ms` | average inference time over the period |
| `heap_free`, `psram_free` | bytes |
| `rssi` | dBm; **omitted** when Wi-Fi is off or not connected |
| `state` | `state_id` as in section 3 |
| `uptime_s` | seconds since boot |

Face box, at most one per 100 ms, only on frames with a face:

```
>box:112,40,96,120,1,0.712
```

`x,y,w,h` in frame pixels (default 320x240, QVGA), then best match `id`
(0 = unknown) and similarity. The box frame and the streamed JPEG are not the
same capture: expect about one frame of lag. The tool hides the box after
500 ms without a new line.

Teleplot's own format is one `>name:value` per line; the comma form is this
project's contract. A Teleplot bridge can split it.

## 6. Event lines (unchanged)

Every `APP_EVENTS` post is logged by `net_comm`:

```
I (12345) net_comm: publish face_unlock/<event> {"id":4,"name":"alice","similarity":0.712,"request":0}
```

`<event>` ∈ `door_opened`, `door_closed`, `face_rejected`, `enroll_started`,
`face_enrolled`, `enroll_failed`, `approval_requested`, `approval_denied`,
`approval_timeout`, `door_fault`. `request` is non-zero only for approval
events.

- `approval_denied` also closes a request that ended another way: a manual
  `unlock` while waiting, or an `approve` for a face deleted in the meantime.
- `enroll_failed` is also posted for a running enroll that a new `enroll`
  replaces.
- `door_fault`: the lock could not be opened (it stays locked). No
  `door_opened` or `door_closed` follows.

Human result lines from `system_logic` that the tool also understands:

```
OPEN for 'alice' (id 4, similarity 0.71)
REJECT unknown face (best similarity 0.40)
Enrolling 'bob': look at the camera
Enrolled 'bob' as id 5 (3 faces)
Enroll 'bob' failed: <reason>
WAIT for approval #123 for 'alice' (similarity 0.71): approve 123 / deny 123
REJECT 'alice': approval #123 denied
```

Boot and network lines the tool keys on:

```
I (300) app_main: Face unlock fcb63ff-dirty (IDF v5.5.1)
I (5123) net_comm: Wi-Fi connected, IP 192.168.1.50
I (6000) app_main: Ready. Type 'help' in the console.
```

## 7. Line classes

| Class | Rule |
|---|---|
| log | `^[EWIDV] \(\d+\) [^:]+: ` |
| telemetry | starts with `>` |
| json reply | starts with `@status ` or `@list ` |
| prompt | buffer ends with `face> ` (no newline) |
| panic | from `Guru Meditation Error:` or `abort() was called` to `Rebooting...` |
| console | anything else (command replies, echo) |

## 8. Panic block

```
Guru Meditation Error: Core  1 panic'ed (LoadProhibited). Exception was unhandled.
Core  1 register dump:
PC      : 0x4200abcd  PS      : 0x00060030  A0      : 0x82001234  A1      : 0x3fcebbd0
...
Backtrace: 0x4200abcd:0x3fcebbd0 0x42001234:0x3fcebbf0 0x42002345:0x3fcebc20

ELF file SHA256: 1a2b3c4d5e6f...
Rebooting...
```

Decode: each `0xPC:0xSP` pair → `xtensa-esp32s3-elf-addr2line -pfiaC -e
build/face_unlock_esp32_s3.elf 0xPC`. Pick the ELF whose SHA-256 starts with the
printed hash. Core dump to UART is off for now (see `sdkconfig.defaults`).

## 9. Camera

`GET http://<ip>/stream` → `multipart/x-mixed-replace;boundary=frameboundary`,
parts `\r\n--frameboundary\r\nContent-Type: image/jpeg\r\nContent-Length: N\r\n\r\n<jpeg>`.
`GET /jpg` → one JPEG. **One HTTP client at a time**: while a stream is open,
every other request waits. The tool keeps a single connection and takes
snapshots from the last received frame.

## 10. Fake board

```bash
python3 mobile_app/tool/fake_board.py --port 8080 --pty-link /tmp/face_unlock_tty
```

Serial console on `/tmp/face_unlock_tty` (a pty: baud rate is ignored, DTR/RTS
fail harmlessly), MJPEG on `http://127.0.0.1:8080/stream`. Extra commands on
the fake only: `panic` (prints section 8 and reboots). Flags: `--seed <n>`
for repeatable events, `--approval` to use the two-step approval flow,
`--no-events` to stop random face events (for scripted tests),
`--elf-sha256 <hex>` for the hash the fake panic prints, `--no-console` for
HTTP only.
