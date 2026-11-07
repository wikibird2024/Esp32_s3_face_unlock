/**
 * @file event_log.h
 * @brief The last EVENT_LOG_CAPACITY app events in RAM, numbered, for the
 *        app's History tab and for polling new events.
 *
 * Plain C with no ESP-IDF includes so it runs in host tests; the caller
 * locks. Numbers (seq) start at 1 on every boot and only grow, so the app
 * asks "everything after seq N" and sees a restart when the newest seq is
 * below what it already has.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_LOG_CAPACITY 50
#define EVENT_LOG_NAME_MAX_LEN 31

typedef struct {
  uint32_t seq;
  /** Board uptime when it happened. */
  uint32_t time_ms;
  /** app_event_id_t value. */
  int32_t type;
  uint16_t face_id;
  float similarity;
  uint32_t request_id;
  char name[EVENT_LOG_NAME_MAX_LEN + 1];
} event_log_entry_t;

typedef struct {
  event_log_entry_t entries[EVENT_LOG_CAPACITY];
  /* Slot the next entry goes into; the oldest entry once full. */
  size_t next_index;
  size_t count;
  uint32_t last_seq;
} event_log_t;

void event_log_init(event_log_t *log);

/** Store a copy (its seq is ignored) over the oldest one when full.
 *  Returns the seq given to it. */
uint32_t event_log_add(event_log_t *log, const event_log_entry_t *entry);

/** Copy the entries with seq > after_seq, oldest first, at most max_count
 *  (the oldest ones are left out when more match). Returns how many. */
size_t event_log_read_after(const event_log_t *log, uint32_t after_seq,
                            event_log_entry_t *out_entries, size_t max_count);

#ifdef __cplusplus
}
#endif
