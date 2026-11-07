# Face Unlock phone app

Flutter app for the ESP32-S3 face unlock door. Author: Hao Tran.

| Tab | State |
|---|---|
| Live | MJPEG from `http://<board>:81/stream` (token), snapshot, Unlock (asks first) |
| Approve | Who waits at the door, Open / Deny, time left. Over MQTT when the broker is connected (instant, signed, works away from home), else polls the board every 2 s |
| Faces | List, Add face (follows the enroll on the board), swipe to delete |
| History | Last 50 board events, newest first; above them the broker's events since the app started |
| Settings | Board address, token, test connection, Broker (MQTT), Camera picture, Unlock rules |

A visitor request over MQTT also shows a phone notification while the app
runs (also in the background). A closed app gets nothing yet (contract
phase D).

```bash
python3 tool/fake_board.py --port 8080    # fake board, no hardware needed
flutter run -d linux                      # address 127.0.0.1:8080, token AB9A9JQ2
flutter analyze && flutter test
```

## MQTT (Adafruit IO)

Contract: `../docs/mqtt_contract.md`.

1. io.adafruit.com → Feeds → create `fu-a1b2c3-approval-request`,
   `fu-a1b2c3-approval-answer`, `fu-a1b2c3-event`, `fu-a1b2c3-status`
   (use your board id instead of `fu-a1b2c3`).
2. App → Settings → Broker (MQTT): Use MQTT on, username, AIO key, board
   id → Test connection → Save.
3. Play the board:

```bash
pip install paho-mqtt
AIO_KEY=<key> python3 tool/fake_mqtt_board.py --user <username> --device fu-a1b2c3
# local: mosquitto -p 1883 &  then  --host 127.0.0.1 --port 1883 --no-tls
python3 tool/fake_mqtt_board.py --self-test      # HMAC vector only
```

Press Enter in the fake board: the Approve tab shows the visitor. With a
phone id + key in Settings and the same key as `--phone-key`, the fake
board checks the signature like the firmware will.

Board API: `../components/board_api/board_api.h`.
Layout: `lib/core/board/` (address, token, API client, MJPEG parser),
`lib/core/mqtt/` (messages + HMAC, settings, `BrokerConnection` seam,
broker notifier), `lib/core/notify/` (phone notifications),
`lib/features/<tab>/` (notifier + `ui/`), `lib/app/` (router, theme). The board serves one viewer at a time, so the
stream runs only while the Live tab is open and the app is in front.
