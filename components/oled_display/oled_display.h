/**
 * @file oled_display.h
 * @brief 0.66" 64x48 SSD1306 OLED on I2C: shows the lock status.
 *
 * Listens to APP_EVENTS only, like feedback: system_logic does not know it
 * exists, and it never touches the lock. Pins are set in menuconfig; with
 * SDA or SCL at -1 nothing starts.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start listening and set up the screen in its own task, so it returns at
 * once even when the screen is missing (the log then says why). Needs the
 * default event loop. Uses I2C port 0 (the camera's SCCB takes port 1). */
esp_err_t oled_display_start(void);

#ifdef __cplusplus
}
#endif
