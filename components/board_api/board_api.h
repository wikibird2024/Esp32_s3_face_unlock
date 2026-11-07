/**
 * @file board_api.h
 * @brief JSON API for the phone app on port 80 (the video stream has its
 *        own server on port 81, so an open stream never blocks the API).
 *
 * Every request needs the app token (see api_token_store.h):
 *   GET    /api/status                 state, door, faces, waiting approval
 *   GET    /api/events?after=<seq>     last 50 events (History, live news)
 *   GET    /api/faces                  enrolled faces
 *   POST   /api/faces/enroll {name}    save the next face seen
 *   POST   /api/faces/enroll/cancel
 *   DELETE /api/faces/<id>
 *   POST   /api/door/unlock            like the console "unlock"
 *   POST   /api/approval {request, approve}
 *   GET|PUT /api/rules                 unlock rules (saved in NVS)
 *   GET|PUT /api/camera                picture settings (saved in NVS)
 * Actions go through system_logic's queue, never straight to the lock.
 * Starts only when Wi-Fi is on.
 */
#pragma once

#include "esp_err.h"
#include "face_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Port of the API server; the stream is on BOARD_API_PORT + 1. */
#define BOARD_API_PORT 80

/**
 * Load or make the token, then start the server on core 0. Call after
 * net_comm_init(). `face_db` may be NULL when recognition did not start;
 * the status then says so.
 */
esp_err_t board_api_start(face_db_t *face_db);

#ifdef __cplusplus
}
#endif
