#include "key_debounce.h"
#include "unity.h"

static key_debounce_t s_debounce;

void setUp(void) { key_debounce_init(&s_debounce); }
void tearDown(void) {}

static char feed(char reading) {
  return key_debounce_update(&s_debounce, reading);
}

static void test_press_is_reported_on_the_third_equal_reading(void) {
  TEST_ASSERT_EQUAL_CHAR(KEY_DEBOUNCE_NO_KEY, feed('5'));
  TEST_ASSERT_EQUAL_CHAR(KEY_DEBOUNCE_NO_KEY, feed('5'));
  TEST_ASSERT_EQUAL_CHAR('5', feed('5'));
}

static void test_held_key_does_not_repeat(void) {
  feed('5');
  feed('5');
  feed('5');
  for (int i = 0; i < 100; i++) {
    TEST_ASSERT_EQUAL_CHAR(KEY_DEBOUNCE_NO_KEY, feed('5'));
  }
}

static void test_contact_bounce_gives_no_key(void) {
  const char bounce[] = {'5', KEY_DEBOUNCE_NO_KEY, '5', KEY_DEBOUNCE_NO_KEY,
                         '5', KEY_DEBOUNCE_NO_KEY};
  for (size_t i = 0; i < sizeof(bounce); i++) {
    TEST_ASSERT_EQUAL_CHAR(KEY_DEBOUNCE_NO_KEY, feed(bounce[i]));
  }
}

static void test_bounce_while_held_does_not_press_again(void) {
  feed('5');
  feed('5');
  feed('5');
  feed(KEY_DEBOUNCE_NO_KEY);
  TEST_ASSERT_EQUAL_CHAR(KEY_DEBOUNCE_NO_KEY, feed('5'));
  TEST_ASSERT_EQUAL_CHAR(KEY_DEBOUNCE_NO_KEY, feed('5'));
  TEST_ASSERT_EQUAL_CHAR(KEY_DEBOUNCE_NO_KEY, feed('5'));
}

static void test_same_key_again_after_a_real_release(void) {
  feed('5');
  feed('5');
  feed('5');
  feed(KEY_DEBOUNCE_NO_KEY);
  feed(KEY_DEBOUNCE_NO_KEY);
  feed(KEY_DEBOUNCE_NO_KEY);
  feed('5');
  feed('5');
  TEST_ASSERT_EQUAL_CHAR('5', feed('5'));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_press_is_reported_on_the_third_equal_reading);
  RUN_TEST(test_held_key_does_not_repeat);
  RUN_TEST(test_contact_bounce_gives_no_key);
  RUN_TEST(test_bounce_while_held_does_not_press_again);
  RUN_TEST(test_same_key_again_after_a_real_release);
  return UNITY_END();
}
