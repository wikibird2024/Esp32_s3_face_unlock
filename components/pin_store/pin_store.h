#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PIN_STORE_MIN_DIGITS 4U
#define PIN_STORE_MAX_DIGITS 8U

/* The door PIN in the default NVS partition, kept only as a salted slow hash,
   with the count of wrong tries. There is no default PIN: until one is set, no
   PIN opens the door. All functions need nvs_flash_init() done and are safe
   from any task. Checking and setting a PIN take a noticeable time, so do not
   call them from an event handler. */

/* True when a readable PIN is stored. */
bool pin_store_has_pin(void);

/* Replaces the PIN and sets the wrong tries to 0. ESP_ERR_INVALID_ARG unless
   digits holds PIN_STORE_MIN_DIGITS to PIN_STORE_MAX_DIGITS digits. */
esp_err_t pin_store_set(const char *digits);

/* Removes the PIN and sets the wrong tries to 0. */
esp_err_t pin_store_clear(void);

/* True only when digits match the stored PIN. */
bool pin_store_check(const char *digits);

/* Wrong tries so far; survives a restart. 0 when nothing is stored. */
uint32_t pin_store_failed_count(void);

esp_err_t pin_store_save_failed_count(uint32_t failed_count);

#ifdef __cplusplus
}
#endif
