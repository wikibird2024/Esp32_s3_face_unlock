# Face Unlock phone app

Flutter app for the ESP32-S3 face unlock door. Author: Hao Tran.

| Tab | State |
|---|---|
| Live | MJPEG from `http://<board>:81/stream` (token), snapshot, Unlock (asks first) |
| Approve | Who waits at the door, Open / Deny, time left (polls every 2 s) |
| Faces | List, Add face (follows the enroll on the board), swipe to delete |
| History | Last 50 board events, newest first |
| Settings | Board address, token, test connection, Camera picture, Unlock rules |

```bash
python3 tool/fake_board.py --port 8080    # fake board, no hardware needed
flutter run -d linux                      # address 127.0.0.1:8080, token AB9A9JQ2
flutter analyze && flutter test
```

Board API: `../components/board_api/board_api.h`.
Layout: `lib/core/board/` (address, token, API client, MJPEG parser), `lib/features/<tab>/`,
`lib/app/` (router, theme). The board serves one viewer at a time, so the
stream runs only while the Live tab is open and the app is in front.
