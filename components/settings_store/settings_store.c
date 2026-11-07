#include "settings_store.h"

#include "esp_check.h"
#include "nvs.h"

static const char *TAG = "settings_store";

#define NVS_NAMESPACE "settings"

esp_err_t settings_store_load(const char *key, void *out_blob,
                              size_t size_bytes) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return ESP_ERR_NOT_FOUND; /* namespace not made yet: nothing saved */
  }
  size_t saved_size_bytes = 0;
  err = nvs_get_blob(handle, key, NULL, &saved_size_bytes);
  if (err == ESP_OK && saved_size_bytes != size_bytes) {
    err = ESP_ERR_NOT_FOUND;
  }
  if (err == ESP_OK) {
    err = nvs_get_blob(handle, key, out_blob, &saved_size_bytes);
  }
  nvs_close(handle);
  return err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : err;
}

esp_err_t settings_store_save(const char *key, const void *blob,
                              size_t size_bytes) {
  nvs_handle_t handle;
  ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG,
                      "nvs open");
  esp_err_t err = nvs_set_blob(handle, key, blob, size_bytes);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}
