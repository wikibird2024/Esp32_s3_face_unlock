#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIN_ENTRY_MIN_DIGITS 4U
#define PIN_ENTRY_MAX_DIGITS 8U
/* Typed digits are forgotten after this long with no key. */
#define PIN_ENTRY_TIMEOUT_MS 10000U
/* This many wrong tries in a row start the lockout. */
#define PIN_ENTRY_FREE_TRIES 5U

/* Plain C, tested on the host. Collects digits, and decides when to check them
   and when to ignore keys. Keys: 0-9 digits, # submit, * clear, A-D nothing.
   Time is passed in, so tests control it. */
typedef enum {
  PIN_ENTRY_ACTION_NONE = 0,
  /* The digit count changed; see digit_count. */
  PIN_ENTRY_ACTION_PROGRESS,
  /* Check digits, then call pin_entry_finish(). */
  PIN_ENTRY_ACTION_CHECK,
  /* Too few digits: refused without counting as a wrong try. */
  PIN_ENTRY_ACTION_REJECT,
  /* A key was pressed during a lockout and was ignored. */
  PIN_ENTRY_ACTION_LOCKED_OUT,
} pin_entry_action_t;

typedef struct {
  char digits[PIN_ENTRY_MAX_DIGITS + 1U];
  uint8_t digit_count;
  uint32_t last_key_ms;
  uint32_t failed_count;
  bool is_locked;
  uint32_t locked_until_ms;
} pin_entry_t;

/* failed_count comes from flash. If it already reached the limit, the lockout
   starts again in full, so cutting the power does not skip it. */
void pin_entry_init(pin_entry_t *entry, uint32_t failed_count, uint32_t now_ms);

pin_entry_action_t pin_entry_key(pin_entry_t *entry, char key, uint32_t now_ms);

/* Call regularly: forgets digits after the timeout and ends the lockout. */
pin_entry_action_t pin_entry_tick(pin_entry_t *entry, uint32_t now_ms);

/* The result of a check: forgets the digits and updates the wrong tries. */
void pin_entry_finish(pin_entry_t *entry, bool is_correct, uint32_t now_ms);

/* Forgets the digits without counting a wrong try. */
void pin_entry_forget_digits(pin_entry_t *entry);

/* For a PIN change by the owner: no more lockout, wrong tries back to 0. */
void pin_entry_clear_lockout(pin_entry_t *entry);

/* How long a lockout lasts after this many wrong tries, 0 when none: 30 s,
   doubling with each further wrong try, up to 1 hour. */
uint32_t pin_entry_lockout_ms(uint32_t failed_count);

#ifdef __cplusplus
}
#endif
