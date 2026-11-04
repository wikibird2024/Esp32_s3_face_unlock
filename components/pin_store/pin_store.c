#include "pin_store.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "nvs.h"

static const char *TAG = "pin_store";

#define PIN_NAMESPACE "door_pin"
#define PIN_RECORD_KEY "record"
#define FAILED_COUNT_KEY "failed"
/* Bump when the record layout or the hash rounds change. An older record then
   counts as no PIN, and the PIN must be set again. */
#define RECORD_FORMAT_VERSION 1U
#define SALT_BYTES 16U
#define HASH_BYTES 32U
/* Slows down guessing from a copy of the flash. A short PIN can still be
   guessed that way; turning on NVS encryption is the real fix. */
#define HASH_ROUNDS 4096U

typedef struct __attribute__((packed)) {
  uint16_t format_version;
  uint8_t salt[SALT_BYTES];
  uint8_t hash[HASH_BYTES];
} pin_record_t;
_Static_assert(sizeof(pin_record_t) == 50U, "PIN record layout changed");

static bool is_valid_pin(const char *digits) {
  if (digits == NULL) {
    return false;
  }
  const size_t digit_count = strlen(digits);
  if (digit_count < PIN_STORE_MIN_DIGITS ||
      digit_count > PIN_STORE_MAX_DIGITS) {
    return false;
  }
  for (size_t i = 0; i < digit_count; i++) {
    if (digits[i] < '0' || digits[i] > '9') {
      return false;
    }
  }
  return true;
}

static esp_err_t compute_hash(const char *digits,
                              const uint8_t salt[SALT_BYTES],
                              uint8_t hash[HASH_BYTES]) {
  const int result = mbedtls_pkcs5_pbkdf2_hmac_ext(
      MBEDTLS_MD_SHA256, (const unsigned char *)digits, strlen(digits), salt,
      SALT_BYTES, HASH_ROUNDS, HASH_BYTES, hash);
  return result == 0 ? ESP_OK : ESP_FAIL;
}

static bool load_record(pin_record_t *record) {
  nvs_handle_t handle;
  if (nvs_open(PIN_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }
  size_t record_size = sizeof(*record);
  const esp_err_t err =
      nvs_get_blob(handle, PIN_RECORD_KEY, record, &record_size);
  nvs_close(handle);
  return err == ESP_OK && record_size == sizeof(*record) &&
         record->format_version == RECORD_FORMAT_VERSION;
}

bool pin_store_has_pin(void) {
  pin_record_t record;
  return load_record(&record);
}

esp_err_t pin_store_set(const char *digits) {
  ESP_RETURN_ON_FALSE(is_valid_pin(digits), ESP_ERR_INVALID_ARG, TAG,
                      "PIN must be %u to %u digits", PIN_STORE_MIN_DIGITS,
                      PIN_STORE_MAX_DIGITS);
  pin_record_t record = {.format_version = RECORD_FORMAT_VERSION};
  esp_fill_random(record.salt, sizeof(record.salt));
  ESP_RETURN_ON_ERROR(compute_hash(digits, record.salt, record.hash), TAG,
                      "hash failed");

  nvs_handle_t handle;
  ESP_RETURN_ON_ERROR(nvs_open(PIN_NAMESPACE, NVS_READWRITE, &handle), TAG,
                      "nvs open failed");
  esp_err_t err = nvs_set_blob(handle, PIN_RECORD_KEY, &record, sizeof(record));
  if (err == ESP_OK) {
    err = nvs_set_u32(handle, FAILED_COUNT_KEY, 0U);
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}

esp_err_t pin_store_clear(void) {
  nvs_handle_t handle;
  ESP_RETURN_ON_ERROR(nvs_open(PIN_NAMESPACE, NVS_READWRITE, &handle), TAG,
                      "nvs open failed");
  esp_err_t err = nvs_erase_all(handle);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}

bool pin_store_check(const char *digits) {
  pin_record_t record;
  if (!is_valid_pin(digits) || !load_record(&record)) {
    return false;
  }
  uint8_t hash[HASH_BYTES];
  if (compute_hash(digits, record.salt, hash) != ESP_OK) {
    return false;
  }
  return mbedtls_ct_memcmp(hash, record.hash, HASH_BYTES) == 0;
}

uint32_t pin_store_failed_count(void) {
  nvs_handle_t handle;
  if (nvs_open(PIN_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
    return 0U;
  }
  uint32_t failed_count = 0U;
  if (nvs_get_u32(handle, FAILED_COUNT_KEY, &failed_count) != ESP_OK) {
    failed_count = 0U;
  }
  nvs_close(handle);
  return failed_count;
}

esp_err_t pin_store_save_failed_count(uint32_t failed_count) {
  nvs_handle_t handle;
  ESP_RETURN_ON_ERROR(nvs_open(PIN_NAMESPACE, NVS_READWRITE, &handle), TAG,
                      "nvs open failed");
  esp_err_t err = nvs_set_u32(handle, FAILED_COUNT_KEY, failed_count);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}
