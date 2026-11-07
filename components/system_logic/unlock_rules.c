#include "unlock_rules.h"

#include <stddef.h>

bool unlock_rules_are_valid(const unlock_rules_t *rules) {
  return rules != NULL &&
         rules->min_similarity_percent >=
             UNLOCK_RULES_MIN_SIMILARITY_PERCENT_MIN &&
         rules->min_similarity_percent <=
             UNLOCK_RULES_MIN_SIMILARITY_PERCENT_MAX &&
         rules->confirm_frames >= UNLOCK_RULES_CONFIRM_FRAMES_MIN &&
         rules->confirm_frames <= UNLOCK_RULES_CONFIRM_FRAMES_MAX &&
         rules->unlock_hold_ms >= UNLOCK_RULES_UNLOCK_HOLD_MS_MIN &&
         rules->unlock_hold_ms <= UNLOCK_RULES_UNLOCK_HOLD_MS_MAX &&
         rules->cooldown_ms <= UNLOCK_RULES_COOLDOWN_MS_MAX &&
         rules->enroll_timeout_ms >= UNLOCK_RULES_ENROLL_TIMEOUT_MS_MIN &&
         rules->enroll_timeout_ms <= UNLOCK_RULES_ENROLL_TIMEOUT_MS_MAX &&
         rules->approval_timeout_ms <= UNLOCK_RULES_APPROVAL_TIMEOUT_MS_MAX;
}

void unlock_rules_to_config(const unlock_rules_t *rules,
                            unlock_config_t *config) {
  config->min_similarity = (float)rules->min_similarity_percent / 100.0f;
  config->confirm_frame_count = rules->confirm_frames;
  config->unlock_hold_ms = rules->unlock_hold_ms;
  config->cooldown_ms = rules->cooldown_ms;
  config->enroll_timeout_ms = rules->enroll_timeout_ms;
  config->approval_timeout_ms = rules->approval_timeout_ms;
}
