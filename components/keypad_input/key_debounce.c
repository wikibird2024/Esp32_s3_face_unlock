#include "key_debounce.h"

#define STABLE_READINGS 3U

void key_debounce_init(key_debounce_t *debounce) {
  debounce->last_reading = KEY_DEBOUNCE_NO_KEY;
  debounce->same_reading_count = 0U;
  debounce->stable_key = KEY_DEBOUNCE_NO_KEY;
}

char key_debounce_update(key_debounce_t *debounce, char reading) {
  if (reading != debounce->last_reading) {
    debounce->last_reading = reading;
    debounce->same_reading_count = 1U;
  } else if (debounce->same_reading_count < STABLE_READINGS) {
    debounce->same_reading_count++;
  }
  if (debounce->same_reading_count < STABLE_READINGS ||
      reading == debounce->stable_key) {
    return KEY_DEBOUNCE_NO_KEY;
  }
  debounce->stable_key = reading;
  return reading;
}
