#include "pin_entry.h"
#include "unity.h"

#include <stdbool.h>
#include <string.h>

#define START_MS 1000U
#define FIRST_LOCKOUT_MS 30000U
#define HOUR_MS 3600000U

static pin_entry_t s_entry;

void setUp(void) { pin_entry_init(&s_entry, 0U, START_MS); }
void tearDown(void) {}

static pin_entry_action_t press_all(const char *keys, uint32_t now_ms) {
  pin_entry_action_t action = PIN_ENTRY_ACTION_NONE;
  for (size_t i = 0; i < strlen(keys); i++) {
    action = pin_entry_key(&s_entry, keys[i], now_ms);
  }
  return action;
}

static void fail_tries(uint32_t try_count) {
  for (uint32_t i = 0U; i < try_count; i++) {
    TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_CHECK, press_all("1111#", START_MS));
    pin_entry_finish(&s_entry, false, START_MS);
  }
}

static void test_digits_are_collected_then_checked(void) {
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_PROGRESS, press_all("1234", START_MS));
  TEST_ASSERT_EQUAL_UINT8(4U, s_entry.digit_count);
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_CHECK,
                    pin_entry_key(&s_entry, '#', START_MS));
  TEST_ASSERT_EQUAL_STRING("1234", s_entry.digits);
}

static void test_finish_forgets_the_digits(void) {
  press_all("1234#", START_MS);
  pin_entry_finish(&s_entry, true, START_MS);
  TEST_ASSERT_EQUAL_UINT8(0U, s_entry.digit_count);
  TEST_ASSERT_EACH_EQUAL_CHAR(0, s_entry.digits, sizeof(s_entry.digits));
}

static void test_star_clears_only_when_there_are_digits(void) {
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_NONE,
                    pin_entry_key(&s_entry, '*', START_MS));
  press_all("12", START_MS);
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_PROGRESS,
                    pin_entry_key(&s_entry, '*', START_MS));
  TEST_ASSERT_EQUAL_UINT8(0U, s_entry.digit_count);
}

static void test_short_pin_is_refused_without_a_wrong_try(void) {
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_REJECT, press_all("123#", START_MS));
  TEST_ASSERT_EQUAL_UINT8(0U, s_entry.digit_count);
  TEST_ASSERT_EQUAL_UINT32(0U, s_entry.failed_count);
}

static void test_hash_with_no_digits_does_nothing(void) {
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_NONE,
                    pin_entry_key(&s_entry, '#', START_MS));
}

static void test_letter_keys_do_nothing(void) {
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_NONE, press_all("ABCD", START_MS));
  TEST_ASSERT_EQUAL_UINT8(0U, s_entry.digit_count);
}

static void test_too_many_digits_stay_inside_the_buffer(void) {
  press_all("12345678", START_MS);
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_NONE,
                    pin_entry_key(&s_entry, '9', START_MS));
  TEST_ASSERT_EQUAL_UINT8(PIN_ENTRY_MAX_DIGITS, s_entry.digit_count);
  TEST_ASSERT_EQUAL_STRING("12345678", s_entry.digits);
}

static void test_digits_are_forgotten_after_the_timeout(void) {
  press_all("12", START_MS);
  TEST_ASSERT_EQUAL(
      PIN_ENTRY_ACTION_NONE,
      pin_entry_tick(&s_entry, START_MS + PIN_ENTRY_TIMEOUT_MS - 1U));
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_PROGRESS,
                    pin_entry_tick(&s_entry, START_MS + PIN_ENTRY_TIMEOUT_MS));
  TEST_ASSERT_EQUAL_UINT8(0U, s_entry.digit_count);
}

static void test_a_new_key_restarts_the_timeout(void) {
  press_all("1", START_MS);
  pin_entry_key(&s_entry, '2', START_MS + 9000U);
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_NONE,
                    pin_entry_tick(&s_entry, START_MS + 11000U));
  TEST_ASSERT_EQUAL_UINT8(2U, s_entry.digit_count);
}

static void test_lockout_starts_on_the_fifth_wrong_try(void) {
  fail_tries(PIN_ENTRY_FREE_TRIES - 1U);
  TEST_ASSERT_FALSE(s_entry.is_locked);
  fail_tries(1U);
  TEST_ASSERT_TRUE(s_entry.is_locked);
}

static void test_keys_are_ignored_during_a_lockout(void) {
  fail_tries(PIN_ENTRY_FREE_TRIES);
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_LOCKED_OUT,
                    pin_entry_key(&s_entry, '1', START_MS + 1U));
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_LOCKED_OUT,
                    pin_entry_key(&s_entry, '#', START_MS + 1U));
  TEST_ASSERT_EQUAL_UINT8(0U, s_entry.digit_count);
}

static void test_keys_work_again_after_the_lockout(void) {
  fail_tries(PIN_ENTRY_FREE_TRIES);
  TEST_ASSERT_EQUAL(
      PIN_ENTRY_ACTION_LOCKED_OUT,
      pin_entry_key(&s_entry, '1', START_MS + FIRST_LOCKOUT_MS - 1U));
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_PROGRESS,
                    pin_entry_key(&s_entry, '1', START_MS + FIRST_LOCKOUT_MS));
}

static void test_lockout_doubles_and_stops_at_one_hour(void) {
  TEST_ASSERT_EQUAL_UINT32(0U, pin_entry_lockout_ms(4U));
  TEST_ASSERT_EQUAL_UINT32(FIRST_LOCKOUT_MS, pin_entry_lockout_ms(5U));
  TEST_ASSERT_EQUAL_UINT32(2U * FIRST_LOCKOUT_MS, pin_entry_lockout_ms(6U));
  TEST_ASSERT_EQUAL_UINT32(64U * FIRST_LOCKOUT_MS, pin_entry_lockout_ms(11U));
  TEST_ASSERT_EQUAL_UINT32(HOUR_MS, pin_entry_lockout_ms(12U));
  TEST_ASSERT_EQUAL_UINT32(HOUR_MS, pin_entry_lockout_ms(UINT32_MAX));
}

static void test_a_restart_does_not_skip_the_lockout(void) {
  pin_entry_init(&s_entry, PIN_ENTRY_FREE_TRIES, START_MS);
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_LOCKED_OUT,
                    pin_entry_key(&s_entry, '1', START_MS + 1U));
  TEST_ASSERT_EQUAL_UINT32(PIN_ENTRY_FREE_TRIES, s_entry.failed_count);
}

static void test_correct_pin_resets_the_wrong_tries(void) {
  fail_tries(PIN_ENTRY_FREE_TRIES - 1U);
  press_all("1234#", START_MS);
  pin_entry_finish(&s_entry, true, START_MS);
  TEST_ASSERT_EQUAL_UINT32(0U, s_entry.failed_count);
  fail_tries(PIN_ENTRY_FREE_TRIES - 1U);
  TEST_ASSERT_FALSE(s_entry.is_locked);
}

static void test_clear_lockout_lets_the_owner_back_in(void) {
  fail_tries(PIN_ENTRY_FREE_TRIES);
  pin_entry_clear_lockout(&s_entry);
  TEST_ASSERT_EQUAL_UINT32(0U, s_entry.failed_count);
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_PROGRESS,
                    pin_entry_key(&s_entry, '1', START_MS + 1U));
}

static void test_timers_work_across_the_clock_wrap(void) {
  const uint32_t near_wrap_ms = UINT32_MAX - 100U;
  pin_entry_init(&s_entry, PIN_ENTRY_FREE_TRIES, near_wrap_ms);
  TEST_ASSERT_EQUAL(PIN_ENTRY_ACTION_LOCKED_OUT,
                    pin_entry_key(&s_entry, '1', near_wrap_ms + 200U));
  TEST_ASSERT_EQUAL(
      PIN_ENTRY_ACTION_PROGRESS,
      pin_entry_key(&s_entry, '1', near_wrap_ms + FIRST_LOCKOUT_MS));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_digits_are_collected_then_checked);
  RUN_TEST(test_finish_forgets_the_digits);
  RUN_TEST(test_star_clears_only_when_there_are_digits);
  RUN_TEST(test_short_pin_is_refused_without_a_wrong_try);
  RUN_TEST(test_hash_with_no_digits_does_nothing);
  RUN_TEST(test_letter_keys_do_nothing);
  RUN_TEST(test_too_many_digits_stay_inside_the_buffer);
  RUN_TEST(test_digits_are_forgotten_after_the_timeout);
  RUN_TEST(test_a_new_key_restarts_the_timeout);
  RUN_TEST(test_lockout_starts_on_the_fifth_wrong_try);
  RUN_TEST(test_keys_are_ignored_during_a_lockout);
  RUN_TEST(test_keys_work_again_after_the_lockout);
  RUN_TEST(test_lockout_doubles_and_stops_at_one_hour);
  RUN_TEST(test_a_restart_does_not_skip_the_lockout);
  RUN_TEST(test_correct_pin_resets_the_wrong_tries);
  RUN_TEST(test_clear_lockout_lets_the_owner_back_in);
  RUN_TEST(test_timers_work_across_the_clock_wrap);
  return UNITY_END();
}
