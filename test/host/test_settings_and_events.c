/* Pure-C parts of the app API: event log ring, unlock rule ranges and
   camera picture ranges. */
#include "camera_settings.h"
#include "event_log.h"
#include "unity.h"
#include "unlock_rules.h"

#include <string.h>

static event_log_t s_log;

void setUp(void) { event_log_init(&s_log); }

void tearDown(void) {}

static void add_event(int32_t type, const char *name) {
  event_log_entry_t entry = {.type = type};
  strncpy(entry.name, name, EVENT_LOG_NAME_MAX_LEN);
  event_log_add(&s_log, &entry);
}

/* ---- event_log --------------------------------------------------------- */

static void test_empty_log_reads_nothing(void) {
  event_log_entry_t out[EVENT_LOG_CAPACITY];
  TEST_ASSERT_EQUAL_size_t(0, event_log_read_after(&s_log, 0, out, 10));
  TEST_ASSERT_EQUAL_UINT32(0, s_log.last_seq);
}

static void test_seq_starts_at_one_and_after_filters(void) {
  add_event(1, "a");
  add_event(2, "b");
  add_event(3, "c");
  event_log_entry_t out[EVENT_LOG_CAPACITY];

  TEST_ASSERT_EQUAL_size_t(3, event_log_read_after(&s_log, 0, out, 10));
  TEST_ASSERT_EQUAL_UINT32(1, out[0].seq);
  TEST_ASSERT_EQUAL_STRING("a", out[0].name);

  TEST_ASSERT_EQUAL_size_t(1, event_log_read_after(&s_log, 2, out, 10));
  TEST_ASSERT_EQUAL_STRING("c", out[0].name);
  TEST_ASSERT_EQUAL_size_t(0, event_log_read_after(&s_log, 3, out, 10));
}

static void test_full_log_drops_the_oldest(void) {
  for (int i = 0; i < EVENT_LOG_CAPACITY + 5; i++) {
    add_event(i, "x");
  }
  event_log_entry_t out[EVENT_LOG_CAPACITY];
  const size_t count = event_log_read_after(&s_log, 0, out, EVENT_LOG_CAPACITY);

  TEST_ASSERT_EQUAL_size_t(EVENT_LOG_CAPACITY, count);
  TEST_ASSERT_EQUAL_UINT32(6, out[0].seq); /* 1-5 dropped */
  TEST_ASSERT_EQUAL_UINT32(EVENT_LOG_CAPACITY + 5, out[count - 1].seq);
  for (size_t i = 1; i < count; i++) {
    TEST_ASSERT_EQUAL_UINT32(out[i - 1].seq + 1, out[i].seq);
  }
}

static void test_max_count_keeps_the_newest(void) {
  for (int i = 0; i < 10; i++) {
    add_event(i, "x");
  }
  event_log_entry_t out[3];
  TEST_ASSERT_EQUAL_size_t(3, event_log_read_after(&s_log, 0, out, 3));
  TEST_ASSERT_EQUAL_UINT32(8, out[0].seq);
  TEST_ASSERT_EQUAL_UINT32(10, out[2].seq);
}

static void test_long_name_is_cut_and_ended(void) {
  event_log_entry_t entry = {.type = 1};
  memset(entry.name, 'n', sizeof(entry.name)); /* no '\0' on purpose */
  event_log_add(&s_log, &entry);
  event_log_entry_t out[1];
  event_log_read_after(&s_log, 0, out, 1);
  TEST_ASSERT_EQUAL_size_t(EVENT_LOG_NAME_MAX_LEN, strlen(out[0].name));
}

/* ---- unlock_rules ------------------------------------------------------ */

static unlock_rules_t default_rules(void) {
  return (unlock_rules_t){.min_similarity_percent = 55,
                          .confirm_frames = 3,
                          .unlock_hold_ms = 3000,
                          .cooldown_ms = 2000,
                          .enroll_timeout_ms = 15000,
                          .approval_timeout_ms = 0};
}

static void test_menuconfig_defaults_are_valid_rules(void) {
  const unlock_rules_t rules = default_rules();
  TEST_ASSERT_TRUE(unlock_rules_are_valid(&rules));
}

static void test_rules_out_of_range_are_refused(void) {
  unlock_rules_t rules = default_rules();
  rules.min_similarity_percent = 29;
  TEST_ASSERT_FALSE(unlock_rules_are_valid(&rules));
  rules = default_rules();
  rules.min_similarity_percent = 96;
  TEST_ASSERT_FALSE(unlock_rules_are_valid(&rules));
  rules = default_rules();
  rules.confirm_frames = 0;
  TEST_ASSERT_FALSE(unlock_rules_are_valid(&rules));
  rules = default_rules();
  rules.unlock_hold_ms = 499; /* a door that barely opens */
  TEST_ASSERT_FALSE(unlock_rules_are_valid(&rules));
  rules = default_rules();
  rules.unlock_hold_ms = 60001; /* a door open too long */
  TEST_ASSERT_FALSE(unlock_rules_are_valid(&rules));
  rules = default_rules();
  rules.approval_timeout_ms = 120001;
  TEST_ASSERT_FALSE(unlock_rules_are_valid(&rules));
  TEST_ASSERT_FALSE(unlock_rules_are_valid(NULL));
}

static void test_rules_fill_the_config_but_keep_the_request_id(void) {
  const unlock_rules_t rules = default_rules();
  unlock_config_t config = {.first_request_id = 1234};
  unlock_rules_to_config(&rules, &config);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.55f, config.min_similarity);
  TEST_ASSERT_EQUAL_UINT8(3, config.confirm_frame_count);
  TEST_ASSERT_EQUAL_UINT32(3000, config.unlock_hold_ms);
  TEST_ASSERT_EQUAL_UINT32(1234, config.first_request_id);
}

/* ---- camera_settings --------------------------------------------------- */

static void test_camera_defaults_and_limits(void) {
  camera_settings_t settings = {.jpeg_quality = 80};
  TEST_ASSERT_TRUE(camera_settings_are_valid(&settings));
  settings.brightness = 3;
  settings.saturation = -4;
  settings.exposure = 5;
  settings.denoise = 8;
  TEST_ASSERT_TRUE(camera_settings_are_valid(&settings));
}

static void test_camera_out_of_range_is_refused(void) {
  camera_settings_t settings = {.jpeg_quality = 80, .brightness = 4};
  TEST_ASSERT_FALSE(camera_settings_are_valid(&settings));
  settings = (camera_settings_t){.jpeg_quality = 80, .denoise = 9};
  TEST_ASSERT_FALSE(camera_settings_are_valid(&settings));
  settings = (camera_settings_t){.jpeg_quality = 9};
  TEST_ASSERT_FALSE(camera_settings_are_valid(&settings));
  settings = (camera_settings_t){.jpeg_quality = 101};
  TEST_ASSERT_FALSE(camera_settings_are_valid(&settings));
  TEST_ASSERT_FALSE(camera_settings_are_valid(NULL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_log_reads_nothing);
  RUN_TEST(test_seq_starts_at_one_and_after_filters);
  RUN_TEST(test_full_log_drops_the_oldest);
  RUN_TEST(test_max_count_keeps_the_newest);
  RUN_TEST(test_long_name_is_cut_and_ended);
  RUN_TEST(test_menuconfig_defaults_are_valid_rules);
  RUN_TEST(test_rules_out_of_range_are_refused);
  RUN_TEST(test_rules_fill_the_config_but_keep_the_request_id);
  RUN_TEST(test_camera_defaults_and_limits);
  RUN_TEST(test_camera_out_of_range_is_refused);
  return UNITY_END();
}
