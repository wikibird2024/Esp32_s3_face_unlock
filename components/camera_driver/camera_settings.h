/**
 * @file camera_settings.h
 * @brief Picture settings the phone app may change, with the ranges the
 *        OV5640 driver (esp32-camera) accepts.
 *
 * Plain C with no ESP-IDF includes so it runs in host tests.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAMERA_SETTINGS_BRIGHTNESS_LIMIT 3
#define CAMERA_SETTINGS_CONTRAST_LIMIT 3
#define CAMERA_SETTINGS_SATURATION_LIMIT 4
#define CAMERA_SETTINGS_SHARPNESS_LIMIT 3
#define CAMERA_SETTINGS_EXPOSURE_LIMIT 5
#define CAMERA_SETTINGS_DENOISE_MAX 8
#define CAMERA_SETTINGS_JPEG_QUALITY_MIN 10
#define CAMERA_SETTINGS_JPEG_QUALITY_MAX 100

typedef struct {
  /** -LIMIT .. +LIMIT, 0 = sensor default. */
  int8_t brightness;
  int8_t contrast;
  int8_t saturation;
  int8_t sharpness;
  /** Auto-exposure target. */
  int8_t exposure;
  /** 0 = off, 1-8 stronger. */
  uint8_t denoise;
  bool vflip;
  bool hmirror;
  /** Video stream and snapshot JPEG quality; recognition is not affected. */
  uint8_t jpeg_quality;
} camera_settings_t;

/** True when every value is inside its range. */
bool camera_settings_are_valid(const camera_settings_t *settings);

#ifdef __cplusplus
}
#endif
