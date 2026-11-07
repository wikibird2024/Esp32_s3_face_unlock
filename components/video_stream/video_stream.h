/**
 * @file video_stream.h
 * @brief Live camera view over HTTP on port 81 for the app or a browser on
 *        the same Wi-Fi: `/` (viewer page), `/stream` (MJPEG), `/jpg`
 *        (snapshot).
 *
 * `/stream` and `/jpg` need the app token (api_token_store.h): header
 * "Authorization: Bearer <token>" or "?token=<token>". In a browser open
 * http://<ip>:81/?token=<token>. Its own server, so an open stream does
 * not block the API on port 80. The stream costs CPU and PSRAM bandwidth
 * only while someone watches; face recognition keeps running.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the HTTP server (core 0, port 81). Call after board_api_start()
 *  (it loads the token) and camera_driver_init(). Does nothing when turned
 *  off in menuconfig. */
esp_err_t video_stream_start(void);

#ifdef __cplusplus
}
#endif
