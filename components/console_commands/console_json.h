/**
 * @file console_json.h
 * @brief Machine-readable console replies (`status json`, `list json`).
 *
 * Plain C with no ESP-IDF dependency, so it runs in host unit tests. The
 * output format is the contract in docs/console_protocol.md.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Enough for a full status line with every field at its longest
 *  (a typical line is about 500 bytes). */
#define CONSOLE_JSON_STATUS_MAX_LEN 768u
/** Worst case per face entry: every name byte escaped plus the keys. */
#define CONSOLE_JSON_LIST_ENTRY_MAX_LEN (FACE_DB_NAME_MAX_LEN * 2u + 24u)

/** Everything the status line needs, gathered by the caller. Strings may
 *  be NULL: `ip` NULL means not connected, others print as "". */
typedef struct {
  const char *state_name;
  int state_id;
  uint32_t frame_count;
  uint32_t face_frame_count;
  uint32_t inference_ms;
  uint16_t last_face_id;
  float last_similarity;
  float min_similarity;
  /** 0 when nothing is waiting. */
  uint32_t waiting_request_id;
  bool is_door_open;
  bool is_wifi_enabled;
  bool is_wifi_connected;
  const char *ip;
  /** false when the face store failed to open: `faces` prints as null. */
  bool has_face_store;
  uint32_t face_count;
  uint32_t face_capacity;
  uint32_t face_skipped;
  uint32_t heap_internal;
  uint32_t heap_psram;
  uint32_t heap_internal_min;
  const char *app_version;
  const char *idf_version;
  const char *compile_date;
  const char *compile_time;
  const char *elf_sha256_short;
  uint64_t uptime_ms;
} console_status_json_t;

/**
 * Write the JSON object (no `@status ` prefix, no newline) into `buffer`.
 * Returns the length, or -1 when it does not fit. The buffer is always
 * 0-terminated when `capacity` > 0.
 */
int console_json_write_status(const console_status_json_t *status, char *buffer,
                              size_t capacity);

/** Write the JSON array of {id, name} objects. Same return rules. */
int console_json_write_list(const face_db_entry_t *entries, size_t count,
                            char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif
