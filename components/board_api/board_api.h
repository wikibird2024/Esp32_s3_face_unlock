/**
 * @file board_api.h
 * @brief JSON API for the phone app on port 80 (the video stream has its
 *        own server on port 81, so an open stream never blocks the API).
 *
 * Every request needs the app token (see api_token_store.h). Today:
 *   GET /api/status  version, state, door, faces, ip, waiting approval
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
