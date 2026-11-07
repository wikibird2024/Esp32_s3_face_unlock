/**
 * @file settings_store.h
 * @brief Saves small settings structs as blobs in the default NVS
 *        partition (namespace "settings"), one key per struct.
 *
 * A blob whose size differs from the struct (the layout changed) is not
 * loaded, so callers fall back to their defaults. Callers check the loaded
 * values themselves.
 */
#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ESP_ERR_NOT_FOUND when nothing was saved or the size differs. */
esp_err_t settings_store_load(const char *key, void *out_blob,
                              size_t size_bytes);

esp_err_t settings_store_save(const char *key, const void *blob,
                              size_t size_bytes);

#ifdef __cplusplus
}
#endif
