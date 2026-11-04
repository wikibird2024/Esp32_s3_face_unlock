/**
 * @file app_events.h
 * @brief Events posted on the default ESP-IDF event loop.
 *
 * system_logic, keypad_input and app_main post them; any component (net_comm,
 * feedback, display) can listen with esp_event_handler_register(APP_EVENTS,
 * ...) without the sender knowing about it.
 */
#pragma once

#include <stdint.h>

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(APP_EVENTS);

#define APP_EVENT_NAME_MAX_LEN 31

typedef enum {
  /** Payload app_event_face_t. face_id 0 means a manual unlock. */
  APP_EVENT_DOOR_OPENED,
  /** No payload. */
  APP_EVENT_DOOR_CLOSED,
  /** Payload app_event_face_t (similarity of the best match). */
  APP_EVENT_FACE_REJECTED,
  /** Payload app_event_face_t (name to be enrolled, face_id 0). */
  APP_EVENT_ENROLL_STARTED,
  /** Payload app_event_face_t (new face id and name). */
  APP_EVENT_FACE_ENROLLED,
  /** Payload app_event_face_t (name). Timeout, cancel or store error. */
  APP_EVENT_ENROLL_FAILED,
  /** Payload app_event_face_t (face, name, request_id). A known face waits
   * for system_logic_request_approve(request_id) or _deny(request_id). */
  APP_EVENT_APPROVAL_REQUESTED,
  /** Payload app_event_face_t (face, name, request_id). */
  APP_EVENT_APPROVAL_DENIED,
  /** Payload app_event_face_t (face, name, request_id). */
  APP_EVENT_APPROVAL_TIMEOUT,
  /** NULL payload. A wrong or too short PIN was entered. */
  APP_EVENT_PIN_REJECTED,
  /** NULL payload. Too many wrong PINs: keys are ignored for a while. */
  APP_EVENT_PIN_LOCKED_OUT,
  /** NULL payload. Face unlock is not running (for example a dead camera). */
  APP_EVENT_RECOGNITION_FAILED,
} app_event_id_t;

typedef struct {
  uint16_t face_id;
  float similarity;
  char name[APP_EVENT_NAME_MAX_LEN + 1];
  /** Approval events: the request id; 0 for other events. */
  uint32_t request_id;
} app_event_face_t;

const char *app_event_name(int32_t event_id);

/* Key presses have their own base because net_comm publishes every APP_EVENT:
   how fast someone types a PIN should not go over the network. */
ESP_EVENT_DECLARE_BASE(KEYPAD_EVENTS);

typedef enum {
  /* Payload keypad_pin_progress_t. */
  KEYPAD_EVENT_PIN_PROGRESS,
} keypad_event_id_t;

/* Only the number of digits, never the digits. 0 means the entry was cleared. */
typedef struct {
  uint8_t digit_count;
} keypad_pin_progress_t;

#ifdef __cplusplus
}
#endif
