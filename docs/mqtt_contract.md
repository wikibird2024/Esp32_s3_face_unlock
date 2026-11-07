# MQTT contract (app side built, firmware not yet)

Author: Hao Tran

The contract between the ESP32-S3 firmware (`net_comm`) and the phone app
for the two-step unlock. The firmware side of the waiting state already
exists (`unlock_state_machine`, state `WAITING_APPROVAL`); today the
console commands `approve <id>` / `deny <id>` stand in for the phone.
The phone app implements its side (phase B, plus signing for phase C);
`mobile_app/tool/fake_mqtt_board.py` plays the board until the firmware
(`net_comm`, phase A) does. Both sides build against this file.

## Naming

`<device>` = `fu-` + last 6 hex digits of the ESP32's Wi-Fi MAC
(for example `fu-a1b2c3`). A topic is `<prefix><name>`; names use only
lowercase letters and dashes, so they are also valid Adafruit IO feed keys.

| Broker | Default prefix | Example topic |
|---|---|---|
| Adafruit IO (`io.adafruit.com`) | `<username>/feeds/<device>-` | `hao/feeds/fu-a1b2c3-event` |
| Any other | `faceunlock/<device>/` | `faceunlock/fu-a1b2c3/event` |

The prefix can be changed in the app (Settings → Broker) and with
`--prefix` in the fake board.

## Topics

| Name | From → to | Payload (JSON) | Retained |
|---|---|---|---|
| `approval-request` | ESP → app | `{"request":1234,"face":4,"name":"alice","similarity":0.71,"expires_s":30}` | no |
| `approval-answer` | app → ESP | `{"request":1234,"approve":true,"phone":"p1","sig":"<hex>"}` | no |
| `event` | ESP → app | `{"type":"door_opened","name":"alice","face":4,"request":1234,"time":"2026-10-22T08:02:11"}` | no |
| `status` | ESP → app | `"online"` / `"offline"` (last will) | yes |

All QoS 1. `event` types are the HTTP event log's (`app_event_name`);
`face` and `request` are 0 when not involved, `time` may be left out
before the clock is set (the app then uses the receive time).
`door_opened`, `approval_denied` and `approval_timeout` with the waiting
`request` end the request in the app. `phone` and `sig` are left out by a
phone that is not paired; the board then must not open.

## Answer signature

```
sig = HMAC-SHA256(phone_key, "<device>|<request>|<approve>")
```

The ESP32 opens the door only when all three hold:

1. `sig` is valid for the key of phone `<phone>` (keys from pairing,
   stored in NVS namespace `phones`; mbedTLS on the ESP32, the same
   test vectors in the app tests and the host tests).
2. Phone `<phone>` is linked to the face in the waiting request.
3. `request` is the id the state machine is waiting for right now
   (already enforced; ids start at a random value each boot).

Test vector (app test, fake board `--self-test`, later the firmware host
test): key `00112233445566778899aabbccddeeff`, `fu-a1b2c3|1234|true` →
`8ad156c76928fc07521359b09ffda63e1606787577cfdbcbdceee72ce6335734`.

## Adafruit IO notes

| Fact | Effect |
|---|---|
| Login = username + AIO key; port 8883 TLS (1883 plain) | App Settings → Broker |
| Free: 30 messages/min for the whole account | One visit ≈ 4 messages; fine for tests |
| Rate limit notices on `<username>/throttle` | The app subscribes and shows them |
| Retain and last will: not confirmed ⚪ | The app shows "board not heard yet" until a `status` arrives |
| Free: at most 10 feeds | Create the 4 feeds by hand: `<device>-approval-request`, `-approval-answer`, `-event`, `-status` |

## Pairing (phase C)

Console `pair <face_id>` → the ESP32 makes a key, shows it as a QR code /
code string → the app scans it and stores the key in the phone's secure
storage → the ESP32 saves phone → face in NVS.

## Phases

| Phase | Content |
|---|---|
| A | Wi-Fi + MQTT in `net_comm`: publish request/event/status, accept answers (test from a PC with mosquitto) |
| B | ✅ Flutter app: connect, show request, Open/Deny, events, local notification |
| C | Pairing + HMAC check (app signs already; board check and QR to do) |
| D | Push notification (app closed) via a small cloud function |
