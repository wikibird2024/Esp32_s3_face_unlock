# Face Unlock Bench (pc_tool)

Desktop tool for the ESP32-S3 face-unlock board: serial console with a
filtered log view and crash decoding, live camera with the face box, build
identity + flash, and a telemetry inspector. Tauri 2 (Rust) + Svelte 5.
Author: Hao Tran.

```
 UI (Svelte, src/)            Rust (src-tauri/)           crates/
 ┌─────────────────┐  invoke  ┌───────────────┐   calls   ┌───────────────┐
 │ Camera │ Build  │ ───────▶ │ thin commands │ ────────▶ │ bench_core    │ serial, MJPEG relay,
 │ Inspector       │ ◀─────── │ (lib.rs)      │ ◀──────── │               │ flash, idf.py, addr2line
 │ Settings        │  events  └───────────────┘  EventSink│ board_protocol│ every parser (pure)
 │ Console dock    │                                      └───────────────┘
 └─────────────────┘
```

The two crates have no Tauri dependency, so `cargo test` for them runs on any
machine. The wire format is `docs/console_protocol.md` in the repo root.

## Run

```bash
# Linux packages for Tauri (once, needs sudo)
sudo apt-get install -y libwebkit2gtk-4.1-dev libjavascriptcoregtk-4.1-dev libsoup-3.0-dev \
  librsvg2-dev libayatana-appindicator3-dev libxdo-dev libudev-dev patchelf

cd pc_tool
npm install
npm run tauri dev            # first Rust build takes several minutes

# Without a board: the fake board (MJPEG + serial console on a pty)
python3 ../mobile_app/tool/fake_board.py --port 8080 --pty-link /tmp/face_unlock_tty
# then in the app: port /tmp/face_unlock_tty, board IP 127.0.0.1:8080
```

With a real board use the UART bridge port (`/dev/ttyUSB0`, CP210x/CH340);
the USB-Serial-JTAG port only mirrors logs on this firmware.

## Check

```bash
cargo test -p board_protocol -p bench_core        # parsers + services, no system libs
cargo clippy -p board_protocol -p bench_core --all-targets -- -D warnings
npm run check && npm run build                     # svelte-check + Vite build
cargo check -p pc_tool                             # needs the apt packages above

# End to end against the fake board (skipped when it is not running)
python3 ../mobile_app/tool/fake_board.py --port 8080 --pty-link /tmp/face_unlock_tty --no-events &
cargo test -p bench_core --test fake_console
```

## Layout

| Path | What |
|---|---|
| `crates/board_protocol/src/` | `line_splitter` (prompt, echo, ESC), `log_line`, `telemetry_line`, `board_event`, `status_json`, `panic_block`, `mjpeg_parser`, `build_files`, `task_table` |
| `crates/bench_core/src/` | `serial/` (manager, reply collector, port list), `camera/relay.rs`, `build_info.rs`, `flash.rs` (esptool), `idf_build.rs`, `backtrace.rs` (addr2line), `events.rs` (event names + payloads) |
| `src-tauri/src/` | `lib.rs` command table, `app_state.rs`, `commands/*.rs` one file per area |
| `src/lib/ipc/` | `types.ts` (wire types), `invoke.ts` (typed commands), `events.ts` (event subscriptions) |
| `src/lib/stores/` | `logStore`, `boardStore`, `telemetryStore`, `settingsStore` (Svelte 5 runes) |
| `src/lib/console/` | filter bar, virtualized log list, command input, panic card |
| `src/lib/camera/`, `build/`, `inspector/`, `settings/` | the four tabs |

Shortcuts: Alt+1..4 tabs, Ctrl+L focus the console, Tab completes a
command, ↑↓ history. The console dock is resizable (drag the divider).

## Decisions

- Flashing spawns `python -m esptool ... write_flash @flash_args` from the
  ESP-IDF python env (newest `~/.espressif/python_env/*`), inside `build/`,
  so the bins and offsets are exactly what `idf.py flash` writes.
- Crash addresses are resolved with the `addr2line` crate reading the ELF's
  DWARF (checked against `build/face_unlock_esp32_s3.elf`: `app_main` →
  `main/main.c`). The toolchain binary is the fallback.
- The camera is relayed by Rust (one connection; the board serves a single
  HTTP client) and shown through blob URLs, so the fps counter, the box
  overlay and snapshots all come from the same frame.
- "Board runs this build" is decided by the ELF SHA-256 prefix from
  `status json`; version strings only hint.

## Not verified yet (⚪)

- Running the Tauri window on this machine: the WebKitGTK dev packages are
  not installed, so `npm run tauri dev` has not been launched. The Tauri
  crate type-checks against stub pkg-config files; the UI has only been
  built with Vite.
- Anything on real hardware: flashing, DTR/RTS reset, USB reconnect after a
  reset, log interleaving at 115200.
