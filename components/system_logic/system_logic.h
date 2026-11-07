/**
 * @file system_logic.h
 * @brief The recognition task: camera → face model → face store → unlock
 *        state machine → door, plus requests from the console and button.
 *
 * One task owns the state machine, so requests from other tasks are queued
 * and handled between frames. Results are posted as APP_EVENTS.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "face_db.h"
#include "unlock_state_machine.h"
#include "unlock_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  unlock_state_t state;
  uint32_t frame_count;
  uint32_t face_frame_count;
  uint32_t last_inference_ms;
  /** Best match of the last frame with a face (0 if none enrolled). */
  uint16_t last_face_id;
  float last_similarity;
  float min_similarity;
  /** Request waiting for approve/deny, 0 if none. */
  uint32_t waiting_request_id;
} system_logic_status_t;

/** Start the recognition task. Camera, face model and door must be ready. */
esp_err_t system_logic_start(face_db_t *face_db);

/** Enroll the next face seen, under `name`. */
esp_err_t system_logic_request_enroll(const char *name);
esp_err_t system_logic_request_cancel_enroll(void);
esp_err_t system_logic_request_unlock(void);

/** Answer the approval request `request_id` (see
 * APP_EVENT_APPROVAL_REQUESTED). Answers to any other request are ignored. */
esp_err_t system_logic_request_approve(uint32_t request_id);
esp_err_t system_logic_request_deny(uint32_t request_id);

/** ESP_ERR_INVALID_STATE when recognition is not running. */
esp_err_t system_logic_get_status(system_logic_status_t *out_status);

/** Unlock rules in use (menuconfig values until the app saves others). */
esp_err_t system_logic_get_rules(unlock_rules_t *out_rules);

/**
 * Check, save (NVS) and apply new rules. Any task; the recognition task
 * applies them on its next loop. ESP_ERR_INVALID_ARG when out of range.
 */
esp_err_t system_logic_set_rules(const unlock_rules_t *rules);

#ifdef __cplusplus
}
#endif
