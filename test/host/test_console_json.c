#include <string.h>

#include "console_json.h"
#include "unity.h"

static char buffer[CONSOLE_JSON_STATUS_MAX_LEN];

void setUp(void) { memset(buffer, 'x', sizeof(buffer)); }

void tearDown(void) {}

static console_status_json_t full_status(void) {
  const console_status_json_t status = {
      .state_name = "waiting approval",
      .state_id = 5,
      .frame_count = 1234,
      .face_frame_count = 56,
      .inference_ms = 85,
      .last_face_id = 1,
      .last_similarity = 0.7124f,
      .min_similarity = 0.55f,
      .waiting_request_id = 4242,
      .is_door_open = true,
      .is_wifi_enabled = true,
      .is_wifi_connected = true,
      .ip = "192.168.1.20",
      .has_face_store = true,
      .face_count = 2,
      .face_capacity = 20,
      .face_skipped = 0,
      .heap_internal = 123456,
      .heap_psram = 7000000,
      .heap_internal_min = 110000,
      .app_version = "fcb63ff-dirty",
      .idf_version = "v5.5.1",
      .compile_date = "Nov  4 2026",
      .compile_time = "16:42:10",
      .elf_sha256_short = "1a2b3c4d",
      .uptime_ms = 123456,
  };
  return status;
}

static void test_status_full(void) {
  const console_status_json_t status = full_status();
  const int length = console_json_write_status(&status, buffer, sizeof(buffer));
  TEST_ASSERT_GREATER_THAN(0, length);
  TEST_ASSERT_EQUAL_STRING(
      "{\"state\":\"waiting approval\",\"state_id\":5,\"frames\":1234,"
      "\"frames_with_face\":56,\"inference_ms\":85,"
      "\"last_match\":{\"id\":1,\"similarity\":0.712,\"threshold\":0.55},"
      "\"approval\":{\"request_id\":4242},\"door\":\"open\","
      "\"wifi\":{\"enabled\":true,\"connected\":true,\"ip\":\"192.168.1.20\"},"
      "\"faces\":{\"count\":2,\"capacity\":20,\"skipped\":0},"
      "\"heap\":{\"internal\":123456,\"psram\":7000000,"
      "\"internal_min\":110000},"
      "\"version\":{\"app\":\"fcb63ff-dirty\",\"idf\":\"v5.5.1\","
      "\"compile_date\":\"Nov  4 2026\",\"compile_time\":\"16:42:10\","
      "\"elf_sha256_short\":\"1a2b3c4d\"},\"uptime_ms\":123456}",
      buffer);
  TEST_ASSERT_EQUAL_INT((int)strlen(buffer), length);
  TEST_ASSERT_NULL(strchr(buffer, '\n'));
}

static void test_status_nulls(void) {
  console_status_json_t status = full_status();
  status.waiting_request_id = 0;
  status.is_door_open = false;
  status.is_wifi_enabled = false;
  status.is_wifi_connected = false;
  status.ip = NULL;
  status.has_face_store = false;
  TEST_ASSERT_GREATER_THAN(
      0, console_json_write_status(&status, buffer, sizeof(buffer)));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"approval\":null"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"door\":\"locked\""));
  TEST_ASSERT_NOT_NULL(strstr(
      buffer, "\"wifi\":{\"enabled\":false,\"connected\":false,\"ip\":null}"));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"faces\":null"));
}

static void test_status_ip_ignored_when_not_connected(void) {
  console_status_json_t status = full_status();
  status.is_wifi_connected = false;
  TEST_ASSERT_GREATER_THAN(
      0, console_json_write_status(&status, buffer, sizeof(buffer)));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"ip\":null"));
}

static void test_status_null_strings_print_empty(void) {
  console_status_json_t status = full_status();
  status.state_name = NULL;
  status.app_version = NULL;
  TEST_ASSERT_GREATER_THAN(
      0, console_json_write_status(&status, buffer, sizeof(buffer)));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "{\"state\":\"\","));
  TEST_ASSERT_NOT_NULL(strstr(buffer, "\"app\":\"\""));
}

static void test_status_too_small(void) {
  const console_status_json_t status = full_status();
  char small[64];
  TEST_ASSERT_EQUAL_INT(
      -1, console_json_write_status(&status, small, sizeof(small)));
  TEST_ASSERT_LESS_THAN(sizeof(small), strlen(small));
  TEST_ASSERT_EQUAL_INT(-1, console_json_write_status(&status, small, 0));
}

static void test_list_empty(void) {
  TEST_ASSERT_EQUAL_INT(
      2, console_json_write_list(NULL, 0, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("[]", buffer);
}

static void test_list_two_entries(void) {
  face_db_entry_t entries[2] = {{.id = 1, .name = "alice"},
                                {.id = 2, .name = "bob"}};
  TEST_ASSERT_GREATER_THAN(
      0, console_json_write_list(entries, 2, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING(
      "[{\"id\":1,\"name\":\"alice\"},{\"id\":2,\"name\":\"bob\"}]", buffer);
}

static void test_list_escapes_name(void) {
  face_db_entry_t entry = {.id = 7,
                           .name = "a\"b\\c\x01"
                                   "d"};
  TEST_ASSERT_GREATER_THAN(
      0, console_json_write_list(&entry, 1, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("[{\"id\":7,\"name\":\"a\\\"b\\\\c?d\"}]", buffer);
}

static void test_list_longest_name_fits_entry_budget(void) {
  face_db_entry_t entry = {.id = 65535};
  memset(entry.name, '"', FACE_DB_NAME_MAX_LEN);
  entry.name[FACE_DB_NAME_MAX_LEN] = '\0';
  char small[CONSOLE_JSON_LIST_ENTRY_MAX_LEN + 2];
  TEST_ASSERT_GREATER_THAN(
      0, console_json_write_list(&entry, 1, small, sizeof(small)));
}

static void test_list_too_small(void) {
  face_db_entry_t entry = {.id = 1, .name = "alice"};
  char small[8];
  TEST_ASSERT_EQUAL_INT(
      -1, console_json_write_list(&entry, 1, small, sizeof(small)));
  TEST_ASSERT_LESS_THAN(sizeof(small), strlen(small));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_status_full);
  RUN_TEST(test_status_nulls);
  RUN_TEST(test_status_ip_ignored_when_not_connected);
  RUN_TEST(test_status_null_strings_print_empty);
  RUN_TEST(test_status_too_small);
  RUN_TEST(test_list_empty);
  RUN_TEST(test_list_two_entries);
  RUN_TEST(test_list_escapes_name);
  RUN_TEST(test_list_longest_name_fits_entry_budget);
  RUN_TEST(test_list_too_small);
  return UNITY_END();
}
