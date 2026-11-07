# face_unlock_esp32_s3

Door lock that opens for enrolled faces. ESP32-S3 + OV5640 camera, face
detection and recognition with Espressif's ESP-DL models (the same models
[esp-who](https://github.com/espressif/esp-who) uses), faces stored in flash.

## How it works

```
 OV5640 ──RGB565 320x240──▶ camera_driver
                                  │ frame
                                  ▼
                        ai_face_recognition  (ESP-DL: detect face → 512-float embedding)
                                  │ embedding
                                  ▼
 console / button ──────request─▶ system_logic task ◀──▶ face_db (cosine match, NVS "face_nvs")
                                  │ unlock_state_machine decides
                                  ▼
                            door_control (GPIO, relocks itself by timer)
                                  │
                           APP_EVENTS (esp_event) ──▶ net_comm (log today, MQTT later)
```

Unlock rules (`unlock_state_machine`):

- The same result must be seen on **3 frames in a row** before acting
  (stops one lucky frame from opening the door).
- Known face (similarity ≥ 0.55) → unlock for 3 s. Unknown face → reject.
- After an unlock, a rejection or an enroll: 2 s cooldown where faces are
  ignored.
- The lock output has its own timer, so it relocks even if the software
  stalls. At boot the lock is set to locked before anything else starts.

All numbers are in `idf.py menuconfig` → *System logic (unlock rules)*.

## Components

| Component | Job | Tested by |
|---|---|---|
| `camera_driver` | OV5640 via esp32-camera, RGB565, pins in Kconfig | device |
| `ai_face_recognition` | ESP-DL face detect + embedding (C++ inside, C API) | device |
| `face_db` | Enrolled faces: add, delete, best match; NVS storage | host + device |
| `system_logic` | Recognition task + unlock state machine | host (state machine) |
| `door_control` | Lock GPIO with auto-relock timer | device |
| `console_commands` | Serial commands for setup and debugging | manual |
| `app_events` | Event ids and payloads shared by all components | — |
| `feedback` | Beeps and an RGB LED (WS2812) from the events (listens only) | manual |
| `oled_display` | 0.66" 64x48 SSD1306 OLED on I2C: lock status from the events (listens only) | host (text drawing) |
| `net_comm` | Wi-Fi station (optional); listens to events, MQTT is the next milestone | — |
| `video_stream` | Live camera view over HTTP on port 81 (`/`, `/stream`, `/jpg`), token required | manual |
| `board_api` | JSON API for the phone app on port 80 (status, events, faces, unlock, approval, rules, camera), token required | host (event log) |
| `settings_store` | Saves the app's rule and camera settings in NVS | — |
| `api_token` | The app token: made once, kept in NVS, checked on every request | host (format, compare, header) |
| `keypad_input` | Stub, not started (next milestone) | — |

## Hardware

| Part | Default | Where to change |
|---|---|---|
| Module | ESP32-S3, ≥ 8 MB flash, **8 MB octal PSRAM** (N8R8 / N16R8) | `sdkconfig.defaults` |
| Camera | OV5640, XCLK 15, SDA 4, SCL 5, D0–D7 11 9 8 10 12 18 17 16, VSYNC 6, HREF 7, PCLK 13 | menuconfig → *Camera driver* |
| Lock output | GPIO 2, active high | menuconfig → *Door control* |
| Enroll button | off (-1); GPIO 0 is BOOT, active low | menuconfig → *System logic* |
| Buzzer, RGB LED | off (-1); free: 14 (buzzer, active high), 21 or the DevKitC LED on 48/38 (WS2812) | menuconfig → *Feedback* |
| OLED 64x48 (SSD1306, I2C 0x3C) | off (-1); free: SDA 41, SCL 42, reset (RS) 40 or tie RS to 3V3 | menuconfig → *OLED display* |

> Quad-PSRAM modules (N8R2, N4R2): set `CONFIG_SPIRAM_MODE_QUAD=y` in
> `sdkconfig.defaults` and delete `sdkconfig`, or the board stops at boot
> with a PSRAM error.

## Build and flash

Needs ESP-IDF **v5.5.x** (v5.3 or newer works for ESP-DL 3.3).

```bash
. $IDF_PATH/export.sh
idf.py build                    # first build downloads ESP-DL, models, esp32-camera
idf.py -p /dev/ttyUSB0 flash monitor
```

After changing `sdkconfig.defaults`, delete `sdkconfig` so the new defaults
are used. Commit the `dependencies.lock` that the first build creates so
everyone builds the same library versions.

## First run

In the serial monitor:

```
face> enroll alice        # then look at the camera for ~1 s
face> list
face> status              # live similarity: use it to tune the threshold
face> unlock              # manual override
face> delete 1
face> clear yes
face> help
```

Or, with an enroll button set in menuconfig, hold it to enroll the next
face as `user_<n>`. The button has no other check, so only use one that
is inside the house.
Enrolling the same person 2–3 times (different light or angle) makes
matching more reliable.

### Watch the camera live (optional)

Set menuconfig → *Network (net_comm)* → Wi-Fi name and password (they stay
in `sdkconfig`, which git ignores). After boot, `status` shows the IP;
the boot log (or the console `token`) shows the **app token**. Open
`http://<ip>:81/?token=<token>` in a browser on the same Wi-Fi for a live
MJPEG view (`/jpg` for a snapshot). The stream server handles one viewer
at a time; the app API on port 80 is a separate server, so it keeps
answering while someone watches. `token new` makes a new token (the app
then needs it again). The token goes over plain HTTP: fine on a home
Wi-Fi, not on a shared one. Face recognition keeps running while
streaming.

### Phone app (`mobile_app/`, Flutter)

Tabs: **Live** (video, snapshot, Unlock), **Approve** (Open / Deny a known
face when "Wait for approval" is above 0), **Faces** (list, add by standing
in front of the camera, swipe to delete), **History** (last 50 events since
the board started) and **Settings** (address, token, camera picture,
unlock rules). Rules and camera settings saved from the app live in NVS
and win over the menuconfig values. Without a board, a fake one on the PC
serves the same API (port N) and stream (port N+1), with the same token
check (`POST /fake/visitor` makes a known face ask for approval):

```bash
cd mobile_app
python3 tool/fake_board.py --port 8080    # app: 127.0.0.1:8080, token AB9A9JQ2
python3 tool/api_check.py --host <board ip> --token <token>   # API check, read-only
flutter run -d linux                      # or plug in a phone: flutter run
flutter analyze && flutter test
```

### Two-step unlock (optional)

Set menuconfig → *System logic* → *Wait for approval after a known face*
to a time in ms (for example 30000). A known face then does not open the
door; it asks for approval and the log shows a request id:

```
system_logic: WAIT for approval #1234 for 'alice' (similarity 0.71): approve 1234 / deny 1234
face> approve 1234        # door opens; "deny 1234" or the timeout rejects
```

Only the current request id counts, so an old answer cannot open the door
later. Today the console gives the answer; a phone (Telegram) is the next
milestone, through the same `system_logic_request_approve()` call.

## Tests

| Suite | Runs on | Command |
|---|---|---|
| Host unit tests: `face_db`, `unlock_state_machine` (50 tests, ASan + UBSan) | PC | see below |
| On-device tests: `face_db` on NVS, `door_control` GPIO + timer | ESP32-S3 | `cd test/target && idf.py build flash monitor` |

```bash
cmake -S test/host -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
# one suite only:
./build-host/test_unlock_state_machine
```

CI (`.github/workflows/ci.yml`) runs the host tests, a clang-format check,
and builds the firmware and the on-device test app in the
`espressif/idf:v5.5.1` container.

## Next milestones

| Milestone | Content |
|---|---|
| Phone approval (phases, see `docs/mqtt_contract.md`) | A: MQTT in `net_comm` → B: Flutter app (`mobile_app/`, live view done) → C: pairing + HMAC-signed answers → D: push notifications |
| Hardening | HTTPS for the app API and stream, console `unlock` off in release, admin-only enroll, flash + NVS encryption |
| Network + security | Wi-Fi provisioning, MQTT over TLS in `net_comm`, OTA (needs a two-slot partition table), NVS encryption for faces, secure boot |
| Keypad | Matrix or single-button keypad: pick GPIOs in Kconfig clear of the camera/lock pins, scan + debounce, key/PIN API or events, PIN unlock with lockout |
| Liveness | Reject photos of a face (blink check or IR camera) |
