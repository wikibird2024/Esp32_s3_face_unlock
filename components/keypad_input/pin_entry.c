#include "pin_entry.h"

#include <string.h>

#define FIRST_LOCKOUT_MS 30000U
#define MAX_LOCKOUT_MS 3600000U

/* True once now_ms has reached end_ms; safe across the 32-bit wrap. */
static bool time_reached(uint32_t now_ms, uint32_t end_ms) {
  return (int32_t)(now_ms - end_ms) >= 0;
}

static void start_lockout_if_due(pin_entry_t *entry, uint32_t now_ms) {
  const uint32_t lockout_ms = pin_entry_lockout_ms(entry->failed_count);
  if (lockout_ms > 0U) {
    entry->is_locked = true;
    entry->locked_until_ms = now_ms + lockout_ms;
  }
}

static void end_finished_lockout(pin_entry_t *entry, uint32_t now_ms) {
  if (entry->is_locked && time_reached(now_ms, entry->locked_until_ms)) {
    entry->is_locked = false;
  }
}

uint32_t pin_entry_lockout_ms(uint32_t failed_count) {
  if (failed_count < PIN_ENTRY_FREE_TRIES) {
    return 0U;
  }
  uint32_t lockout_ms = FIRST_LOCKOUT_MS;
  for (uint32_t i = PIN_ENTRY_FREE_TRIES;
       i < failed_count && lockout_ms < MAX_LOCKOUT_MS; i++) {
    lockout_ms *= 2U;
  }
  return lockout_ms < MAX_LOCKOUT_MS ? lockout_ms : MAX_LOCKOUT_MS;
}

void pin_entry_forget_digits(pin_entry_t *entry) {
  memset(entry->digits, 0, sizeof(entry->digits));
  entry->digit_count = 0U;
}

void pin_entry_init(pin_entry_t *entry, uint32_t failed_count,
                    uint32_t now_ms) {
  memset(entry, 0, sizeof(*entry));
  entry->failed_count = failed_count;
  start_lockout_if_due(entry, now_ms);
}

pin_entry_action_t pin_entry_key(pin_entry_t *entry, char key,
                                 uint32_t now_ms) {
  end_finished_lockout(entry, now_ms);
  if (entry->is_locked) {
    return PIN_ENTRY_ACTION_LOCKED_OUT;
  }
  entry->last_key_ms = now_ms;

  if (key >= '0' && key <= '9') {
    if (entry->digit_count >= PIN_ENTRY_MAX_DIGITS) {
      return PIN_ENTRY_ACTION_NONE;
    }
    entry->digits[entry->digit_count] = key;
    entry->digit_count++;
    return PIN_ENTRY_ACTION_PROGRESS;
  }
  if (key == '*') {
    const bool had_digits = entry->digit_count > 0U;
    pin_entry_forget_digits(entry);
    return had_digits ? PIN_ENTRY_ACTION_PROGRESS : PIN_ENTRY_ACTION_NONE;
  }
  if (key == '#') {
    if (entry->digit_count == 0U) {
      return PIN_ENTRY_ACTION_NONE;
    }
    if (entry->digit_count < PIN_ENTRY_MIN_DIGITS) {
      pin_entry_forget_digits(entry);
      return PIN_ENTRY_ACTION_REJECT;
    }
    return PIN_ENTRY_ACTION_CHECK;
  }
  return PIN_ENTRY_ACTION_NONE;
}

pin_entry_action_t pin_entry_tick(pin_entry_t *entry, uint32_t now_ms) {
  end_finished_lockout(entry, now_ms);
  if (entry->digit_count > 0U &&
      time_reached(now_ms, entry->last_key_ms + PIN_ENTRY_TIMEOUT_MS)) {
    pin_entry_forget_digits(entry);
    return PIN_ENTRY_ACTION_PROGRESS;
  }
  return PIN_ENTRY_ACTION_NONE;
}

void pin_entry_finish(pin_entry_t *entry, bool is_correct, uint32_t now_ms) {
  pin_entry_forget_digits(entry);
  if (is_correct) {
    pin_entry_clear_lockout(entry);
    return;
  }
  entry->failed_count++;
  start_lockout_if_due(entry, now_ms);
}

void pin_entry_clear_lockout(pin_entry_t *entry) {
  entry->failed_count = 0U;
  entry->is_locked = false;
}
