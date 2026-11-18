#include "console_json.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Grows a string in a fixed buffer; once it overflows it stays overflowed
   so the caller only checks at the end. */
typedef struct {
  char *buffer;
  size_t capacity;
  size_t length;
  bool is_overflowed;
} json_cursor_t;

static void cursor_init(json_cursor_t *cursor, char *buffer, size_t capacity) {
  cursor->buffer = buffer;
  cursor->capacity = capacity;
  cursor->length = 0;
  cursor->is_overflowed = capacity == 0;
  if (capacity > 0) {
    buffer[0] = '\0';
  }
}

static void append_char(json_cursor_t *cursor, char character) {
  if (cursor->is_overflowed || cursor->length + 1 >= cursor->capacity) {
    cursor->is_overflowed = true;
    return;
  }
  cursor->buffer[cursor->length++] = character;
  cursor->buffer[cursor->length] = '\0';
}

static void append_text(json_cursor_t *cursor, const char *text) {
  for (; *text != '\0'; text++) {
    append_char(cursor, *text);
  }
}

static void append_formatted(json_cursor_t *cursor, const char *format, ...) {
  if (cursor->is_overflowed) {
    return;
  }
  va_list arguments;
  va_start(arguments, format);
  const size_t room = cursor->capacity - cursor->length;
  const int written =
      vsnprintf(cursor->buffer + cursor->length, room, format, arguments);
  va_end(arguments);
  if (written < 0 || (size_t)written >= room) {
    cursor->is_overflowed = true;
    cursor->buffer[cursor->length] = '\0';
    return;
  }
  cursor->length += (size_t)written;
}

/* Escapes only what JSON requires for the characters a name can hold;
   control bytes become '?' so a broken record cannot break the line. */
static void append_quoted(json_cursor_t *cursor, const char *text) {
  append_char(cursor, '"');
  for (; text != NULL && *text != '\0'; text++) {
    const unsigned char byte = (unsigned char)*text;
    if (byte == '"' || byte == '\\') {
      append_char(cursor, '\\');
    }
    append_char(cursor, byte < 0x20 ? '?' : *text);
  }
  append_char(cursor, '"');
}

static int finish(json_cursor_t *cursor) {
  return cursor->is_overflowed ? -1 : (int)cursor->length;
}

int console_json_write_status(const console_status_json_t *status, char *buffer,
                              size_t capacity) {
  if (status == NULL || buffer == NULL) {
    return -1;
  }
  json_cursor_t cursor;
  cursor_init(&cursor, buffer, capacity);

  append_text(&cursor, "{\"state\":");
  append_quoted(&cursor, status->state_name);
  append_formatted(&cursor,
                   ",\"state_id\":%d,\"frames\":%" PRIu32
                   ",\"frames_with_face\":%" PRIu32
                   ",\"inference_ms\":%" PRIu32,
                   status->state_id, status->frame_count,
                   status->face_frame_count, status->inference_ms);
  append_formatted(&cursor,
                   ",\"last_match\":{\"id\":%u,\"similarity\":%.3f,"
                   "\"threshold\":%.2f}",
                   (unsigned)status->last_face_id,
                   (double)status->last_similarity,
                   (double)status->min_similarity);
  if (status->waiting_request_id != 0) {
    append_formatted(&cursor, ",\"approval\":{\"request_id\":%" PRIu32 "}",
                     status->waiting_request_id);
  } else {
    append_text(&cursor, ",\"approval\":null");
  }
  append_formatted(&cursor, ",\"door\":\"%s\"",
                   status->is_door_open ? "open" : "locked");

  append_formatted(&cursor,
                   ",\"wifi\":{\"enabled\":%s,\"connected\":%s,\"ip\":",
                   status->is_wifi_enabled ? "true" : "false",
                   status->is_wifi_connected ? "true" : "false");
  if (status->is_wifi_connected && status->ip != NULL) {
    append_quoted(&cursor, status->ip);
  } else {
    append_text(&cursor, "null");
  }
  append_char(&cursor, '}');

  if (status->has_face_store) {
    append_formatted(&cursor,
                     ",\"faces\":{\"count\":%" PRIu32 ",\"capacity\":%" PRIu32
                     ",\"skipped\":%" PRIu32 "}",
                     status->face_count, status->face_capacity,
                     status->face_skipped);
  } else {
    append_text(&cursor, ",\"faces\":null");
  }

  append_formatted(&cursor,
                   ",\"heap\":{\"internal\":%" PRIu32 ",\"psram\":%" PRIu32
                   ",\"internal_min\":%" PRIu32 "}",
                   status->heap_internal, status->heap_psram,
                   status->heap_internal_min);

  append_text(&cursor, ",\"version\":{\"app\":");
  append_quoted(&cursor, status->app_version);
  append_text(&cursor, ",\"idf\":");
  append_quoted(&cursor, status->idf_version);
  append_text(&cursor, ",\"compile_date\":");
  append_quoted(&cursor, status->compile_date);
  append_text(&cursor, ",\"compile_time\":");
  append_quoted(&cursor, status->compile_time);
  append_text(&cursor, ",\"elf_sha256_short\":");
  append_quoted(&cursor, status->elf_sha256_short);
  append_char(&cursor, '}');

  append_formatted(&cursor, ",\"uptime_ms\":%" PRIu64 "}", status->uptime_ms);
  return finish(&cursor);
}

int console_json_write_list(const face_db_entry_t *entries, size_t count,
                            char *buffer, size_t capacity) {
  if (buffer == NULL || (entries == NULL && count > 0)) {
    return -1;
  }
  json_cursor_t cursor;
  cursor_init(&cursor, buffer, capacity);
  append_char(&cursor, '[');
  for (size_t i = 0; i < count; i++) {
    if (i > 0) {
      append_char(&cursor, ',');
    }
    append_formatted(&cursor, "{\"id\":%u,\"name\":", (unsigned)entries[i].id);
    append_quoted(&cursor, entries[i].name);
    append_char(&cursor, '}');
  }
  append_char(&cursor, ']');
  return finish(&cursor);
}
