#include "display_render.h"
#include "unity.h"

#include <stdbool.h>
#include <string.h>

static uint8_t s_pixels[DISPLAY_RENDER_BUFFER_BYTES];

void setUp(void) { memset(s_pixels, 0xFF, sizeof(s_pixels)); }
void tearDown(void) {}

static bool is_lit(uint32_t x_px, uint32_t y_px) {
  const uint8_t column = s_pixels[(y_px / 8U) * DISPLAY_RENDER_WIDTH_PX + x_px];
  return (column & (1U << (y_px % 8U))) != 0U;
}

static uint32_t count_lit(void) {
  uint32_t lit_count = 0U;
  for (uint32_t x_px = 0U; x_px < DISPLAY_RENDER_WIDTH_PX; x_px++) {
    for (uint32_t y_px = 0U; y_px < DISPLAY_RENDER_HEIGHT_PX; y_px++) {
      lit_count += is_lit(x_px, y_px) ? 1U : 0U;
    }
  }
  return lit_count;
}

static void test_four_characters_fill_the_width_and_stay_centered(void) {
  display_render_text("OPEN", s_pixels);

  TEST_ASSERT_TRUE(is_lit(0U, 19U));   /* left edge of O */
  TEST_ASSERT_TRUE(is_lit(62U, 13U));  /* top right of N */
  TEST_ASSERT_TRUE(is_lit(62U, 33U));  /* bottom right of N */
  TEST_ASSERT_FALSE(is_lit(63U, 13U)); /* nothing past the word */

  for (uint32_t x_px = 0U; x_px < DISPLAY_RENDER_WIDTH_PX; x_px++) {
    TEST_ASSERT_FALSE(is_lit(x_px, 12U));
    TEST_ASSERT_FALSE(is_lit(x_px, 34U));
  }
}

static void test_short_word_is_centered_and_has_the_right_shape(void) {
  /* T: bar on the top row, a stem 3 px wide in the middle. */
  display_render_text("T", s_pixels);

  TEST_ASSERT_FALSE(is_lit(23U, 13U));
  TEST_ASSERT_TRUE(is_lit(24U, 13U));
  TEST_ASSERT_TRUE(is_lit(38U, 13U));
  TEST_ASSERT_FALSE(is_lit(39U, 13U));
  TEST_ASSERT_FALSE(is_lit(24U, 16U));
  TEST_ASSERT_TRUE(is_lit(30U, 33U));
  TEST_ASSERT_TRUE(is_lit(32U, 33U));
  TEST_ASSERT_FALSE(is_lit(29U, 33U));
  TEST_ASSERT_FALSE(is_lit(33U, 33U));
}

static void test_every_character_of_every_word_has_a_shape(void) {
  /* A character with no shape would leave a silent gap on the door. */
  for (int word = 0; word < DISPLAY_WORD_COUNT; word++) {
    const char *text = display_render_word_text((display_word_t)word);
    TEST_ASSERT_GREATER_THAN_size_t(0U, strlen(text));
    TEST_ASSERT_LESS_OR_EQUAL_size_t(4U, strlen(text));
    for (size_t i = 0; i < strlen(text); i++) {
      TEST_ASSERT_TRUE_MESSAGE(display_render_has_shape(text[i]), text);
    }
  }
}

static void test_ip_address_and_pin_stars_have_shapes(void) {
  const char *characters = "0123456789.*";
  for (size_t i = 0; i < strlen(characters); i++) {
    TEST_ASSERT_TRUE(display_render_has_shape(characters[i]));
  }
}

static void test_old_pixels_are_cleared(void) {
  display_render_text("", s_pixels);
  TEST_ASSERT_EQUAL_UINT32(0U, count_lit());
}

static void test_character_with_no_shape_stays_blank(void) {
  display_render_text("?", s_pixels);
  TEST_ASSERT_EQUAL_UINT32(0U, count_lit());
}

static void test_long_text_is_small_and_wraps_after_ten_characters(void) {
  /* The stem of a 1 is the third column of its shape. Two lines start at y 15
     and y 25, and the text starts at x 2. */
  display_render_text("11111111111", s_pixels);

  TEST_ASSERT_TRUE(is_lit(4U, 15U));
  TEST_ASSERT_TRUE(is_lit(58U, 15U)); /* last character of the first line */
  TEST_ASSERT_TRUE(is_lit(4U, 25U));  /* wrapped to the second line */
  TEST_ASSERT_FALSE(is_lit(10U, 25U));
}

static void test_longest_ip_address_stays_on_the_screen(void) {
  /* The sanitizers in the host build catch any write past the buffer. */
  display_render_text("255.255.255.255", s_pixels);
  TEST_ASSERT_GREATER_THAN_UINT32(0U, count_lit());
}

static void test_text_past_four_lines_is_cut(void) {
  uint8_t four_lines[DISPLAY_RENDER_BUFFER_BYTES];
  display_render_text("1111111111222222222233333333334444444444", four_lines);

  display_render_text("1111111111222222222233333333334444444444555", s_pixels);

  TEST_ASSERT_EQUAL_UINT8_ARRAY(four_lines, s_pixels,
                                DISPLAY_RENDER_BUFFER_BYTES);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_four_characters_fill_the_width_and_stay_centered);
  RUN_TEST(test_short_word_is_centered_and_has_the_right_shape);
  RUN_TEST(test_every_character_of_every_word_has_a_shape);
  RUN_TEST(test_ip_address_and_pin_stars_have_shapes);
  RUN_TEST(test_old_pixels_are_cleared);
  RUN_TEST(test_character_with_no_shape_stays_blank);
  RUN_TEST(test_long_text_is_small_and_wraps_after_ten_characters);
  RUN_TEST(test_longest_ip_address_stays_on_the_screen);
  RUN_TEST(test_text_past_four_lines_is_cut);
  return UNITY_END();
}
