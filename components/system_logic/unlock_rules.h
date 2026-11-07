/**
 * @file unlock_rules.h
 * @brief The unlock rules the phone app may change, with their allowed
 *        ranges.
 *
 * Plain C with no ESP-IDF includes so it runs in host tests. The ranges
 * are the same as in Kconfig (menu "System logic"); keep both in step.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "unlock_state_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UNLOCK_RULES_MIN_SIMILARITY_PERCENT_MIN 30
#define UNLOCK_RULES_MIN_SIMILARITY_PERCENT_MAX 95
#define UNLOCK_RULES_CONFIRM_FRAMES_MIN 1
#define UNLOCK_RULES_CONFIRM_FRAMES_MAX 10
#define UNLOCK_RULES_UNLOCK_HOLD_MS_MIN 500
#define UNLOCK_RULES_UNLOCK_HOLD_MS_MAX 60000
#define UNLOCK_RULES_COOLDOWN_MS_MAX 60000
#define UNLOCK_RULES_ENROLL_TIMEOUT_MS_MIN 1000
#define UNLOCK_RULES_ENROLL_TIMEOUT_MS_MAX 120000
#define UNLOCK_RULES_APPROVAL_TIMEOUT_MS_MAX 120000

typedef struct {
  uint8_t min_similarity_percent;
  uint8_t confirm_frames;
  uint32_t unlock_hold_ms;
  uint32_t cooldown_ms;
  uint32_t enroll_timeout_ms;
  /** 0 = a known face opens at once (no approval step). */
  uint32_t approval_timeout_ms;
} unlock_rules_t;

/** True when every value is inside its range. */
bool unlock_rules_are_valid(const unlock_rules_t *rules);

/** Copy the rules into a state machine config; first_request_id is kept. */
void unlock_rules_to_config(const unlock_rules_t *rules,
                            unlock_config_t *config);

#ifdef __cplusplus
}
#endif
