#include "display_text.h"
#include "unity.h"

#include <string.h>

static char s_lines[DISPLAY_LINE_COUNT][DISPLAY_LINE_CHARS + 1];
static uint8_t s_frame[DISPLAY_FRAME_BYTES];

void setUp(void) {
  memset(s_lines, 0, sizeof(s_lines));
  memset(s_frame, 0xAA, sizeof(s_frame));
}

void tearDown(void) {}

static int lit_columns_in_row(int line) {
  int count = 0;
  for (int x = 0; x < DISPLAY_WIDTH_PX; x++) {
    count += s_frame[line * DISPLAY_WIDTH_PX + x] != 0;
  }
  return count;
}

static void test_blank_lines_clear_the_frame(void) {
  display_text_render(s_lines, s_frame);
  for (int i = 0; i < DISPLAY_FRAME_BYTES; i++) {
    TEST_ASSERT_EQUAL_HEX8(0, s_frame[i]);
  }
}

static void test_one_char_is_centered_on_its_row(void) {
  strcpy(s_lines[2], "I");
  display_text_render(s_lines, s_frame);
  /* 'I' is 00 41 7F 41 00; the 5-pixel cell starts at (64 - 5) / 2 = 29. */
  const uint8_t *row = &s_frame[2 * DISPLAY_WIDTH_PX];
  TEST_ASSERT_EQUAL_HEX8(0x41, row[30]);
  TEST_ASSERT_EQUAL_HEX8(0x7F, row[31]);
  TEST_ASSERT_EQUAL_HEX8(0x41, row[32]);
  TEST_ASSERT_EQUAL_INT(3, lit_columns_in_row(2));
  TEST_ASSERT_EQUAL_INT(0, lit_columns_in_row(1));
  TEST_ASSERT_EQUAL_INT(0, lit_columns_in_row(3));
}

static void test_full_line_fits_inside_the_screen(void) {
  strcpy(s_lines[0], "MMMMMMMMMM");
  display_text_render(s_lines, s_frame);
  /* 10 'M' cells (5 lit columns each) leave 2 blank columns on the left. */
  TEST_ASSERT_EQUAL_INT(10 * 5, lit_columns_in_row(0));
  TEST_ASSERT_EQUAL_HEX8(0, s_frame[1]);
  TEST_ASSERT_EQUAL_HEX8(0x7F, s_frame[2]);
}

static void test_non_ascii_is_drawn_as_question_mark(void) {
  char expected_lines[DISPLAY_LINE_COUNT][DISPLAY_LINE_CHARS + 1] = {{0}};
  uint8_t expected_frame[DISPLAY_FRAME_BYTES];
  strcpy(expected_lines[4], "a?b");
  display_text_render(expected_lines, expected_frame);

  strcpy(s_lines[4], "a\001b");
  display_text_render(s_lines, s_frame);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_frame, s_frame, DISPLAY_FRAME_BYTES);

  s_lines[4][1] = (char)0xC3; /* first byte of a UTF-8 letter */
  display_text_render(s_lines, s_frame);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_frame, s_frame, DISPLAY_FRAME_BYTES);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_blank_lines_clear_the_frame);
  RUN_TEST(test_one_char_is_centered_on_its_row);
  RUN_TEST(test_full_line_fits_inside_the_screen);
  RUN_TEST(test_non_ascii_is_drawn_as_question_mark);
  return UNITY_END();
}
