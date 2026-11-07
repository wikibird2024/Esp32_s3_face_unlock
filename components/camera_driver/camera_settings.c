#include "camera_settings.h"

#include <stddef.h>

static bool is_within(int value, int limit) {
  return value >= -limit && value <= limit;
}

bool camera_settings_are_valid(const camera_settings_t *settings) {
  return settings != NULL &&
         is_within(settings->brightness, CAMERA_SETTINGS_BRIGHTNESS_LIMIT) &&
         is_within(settings->contrast, CAMERA_SETTINGS_CONTRAST_LIMIT) &&
         is_within(settings->saturation, CAMERA_SETTINGS_SATURATION_LIMIT) &&
         is_within(settings->sharpness, CAMERA_SETTINGS_SHARPNESS_LIMIT) &&
         is_within(settings->exposure, CAMERA_SETTINGS_EXPOSURE_LIMIT) &&
         settings->denoise <= CAMERA_SETTINGS_DENOISE_MAX &&
         settings->jpeg_quality >= CAMERA_SETTINGS_JPEG_QUALITY_MIN &&
         settings->jpeg_quality <= CAMERA_SETTINGS_JPEG_QUALITY_MAX;
}
