#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEY_DEBOUNCE_NO_KEY '\0'

/* Plain C, tested on the host. Turns raw scans into one result per press. */
typedef struct {
  char last_reading;
  uint8_t same_reading_count;
  char stable_key;
} key_debounce_t;

void key_debounce_init(key_debounce_t *debounce);

/* Call once per scan with the key seen, or KEY_DEBOUNCE_NO_KEY. Returns the key
   once when a press becomes stable, otherwise KEY_DEBOUNCE_NO_KEY. A held key
   does not repeat. */
char key_debounce_update(key_debounce_t *debounce, char reading);

#ifdef __cplusplus
}
#endif
