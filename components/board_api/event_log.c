#include "event_log.h"

#include <string.h>

void event_log_init(event_log_t *log) { memset(log, 0, sizeof(*log)); }

uint32_t event_log_add(event_log_t *log, const event_log_entry_t *entry) {
  event_log_entry_t *slot = &log->entries[log->next_index];
  *slot = *entry;
  slot->name[EVENT_LOG_NAME_MAX_LEN] = '\0';
  log->last_seq++;
  slot->seq = log->last_seq;
  log->next_index = (log->next_index + 1) % EVENT_LOG_CAPACITY;
  if (log->count < EVENT_LOG_CAPACITY) {
    log->count++;
  }
  return slot->seq;
}

size_t event_log_read_after(const event_log_t *log, uint32_t after_seq,
                            event_log_entry_t *out_entries, size_t max_count) {
  /* Oldest entry: slot 0 until the ring is full, then next_index. */
  const size_t oldest_index =
      log->count < EVENT_LOG_CAPACITY ? 0 : log->next_index;
  size_t match_count = 0;
  for (size_t i = 0; i < log->count; i++) {
    if (log->entries[(oldest_index + i) % EVENT_LOG_CAPACITY].seq > after_seq) {
      match_count++;
    }
  }
  /* Keep the newest max_count of the matches. */
  size_t skip_count = match_count > max_count ? match_count - max_count : 0;
  size_t copied_count = 0;
  for (size_t i = 0; i < log->count && copied_count < max_count; i++) {
    const event_log_entry_t *entry =
        &log->entries[(oldest_index + i) % EVENT_LOG_CAPACITY];
    if (entry->seq <= after_seq) {
      continue;
    }
    if (skip_count > 0) {
      skip_count--;
      continue;
    }
    out_entries[copied_count++] = *entry;
  }
  return copied_count;
}
