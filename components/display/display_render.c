#include "display_render.h"

#include <stddef.h>
#include <string.h>

#define SHAPE_WIDTH_PX 5U
#define SHAPE_HEIGHT_PX 7U
#define PIXELS_PER_BYTE 8U

/* Big text: each shape pixel is 3x3 screen pixels, so 4 characters fill the
   64 pixel width: 4 x 15 + 3 gaps of 1 = 63. */
#define BIG_MAX_CHARACTERS 4U
#define BIG_SCALE 3U
#define BIG_GAP_PX 1U

#define SMALL_SCALE 1U
#define SMALL_STEP_PX 6U
#define SMALL_LINE_STEP_PX 10U
#define SMALL_CHARACTERS_PER_LINE 10U
#define SMALL_MAX_LINES 4U

typedef struct {
  char character;
  uint8_t columns[SHAPE_WIDTH_PX];
} shape_t;

/* Only the characters the screen needs. One byte per column, lowest bit on
   top. */
static const shape_t SHAPES[] = {
    {'*', {0x14, 0x08, 0x3E, 0x08, 0x14}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
};

static const char *const WORD_TEXTS[DISPLAY_WORD_COUNT] = {
    [DISPLAY_WORD_OPEN] = "OPEN", [DISPLAY_WORD_DENY] = "DENY",
    [DISPLAY_WORD_TIME] = "TIME", [DISPLAY_WORD_WAIT] = "WAIT",
    [DISPLAY_WORD_SCAN] = "SCAN", [DISPLAY_WORD_DONE] = "DONE",
    [DISPLAY_WORD_FAIL] = "FAIL", [DISPLAY_WORD_LOCK] = "LOCK",
    [DISPLAY_WORD_ERROR] = "ERR",
};

static const shape_t *find_shape(char character) {
  const size_t shape_count = sizeof(SHAPES) / sizeof(SHAPES[0]);
  for (size_t i = 0; i < shape_count; i++) {
    if (SHAPES[i].character == character) {
      return &SHAPES[i];
    }
  }
  return NULL;
}

static void set_pixel(uint8_t *pixels, uint32_t x_px, uint32_t y_px) {
  const uint32_t index =
      (y_px / PIXELS_PER_BYTE) * DISPLAY_RENDER_WIDTH_PX + x_px;
  const uint32_t mask = 1U << (y_px % PIXELS_PER_BYTE);
  pixels[index] = (uint8_t)(pixels[index] | mask);
}

static void draw_character(char character, uint32_t left_px, uint32_t top_px,
                           uint32_t scale, uint8_t *pixels) {
  const shape_t *shape = find_shape(character);
  if (shape == NULL) {
    return;
  }
  for (uint32_t column = 0; column < SHAPE_WIDTH_PX; column++) {
    for (uint32_t row = 0; row < SHAPE_HEIGHT_PX; row++) {
      if ((shape->columns[column] & (1U << row)) == 0U) {
        continue;
      }
      for (uint32_t x_step = 0; x_step < scale; x_step++) {
        for (uint32_t y_step = 0; y_step < scale; y_step++) {
          set_pixel(pixels, left_px + column * scale + x_step,
                    top_px + row * scale + y_step);
        }
      }
    }
  }
}

static void draw_big(const char *text, uint32_t character_count,
                     uint8_t *pixels) {
  const uint32_t step_px = SHAPE_WIDTH_PX * BIG_SCALE + BIG_GAP_PX;
  const uint32_t text_width_px = character_count * step_px - BIG_GAP_PX;
  const uint32_t left_px = (DISPLAY_RENDER_WIDTH_PX - text_width_px) / 2U;
  const uint32_t top_px =
      (DISPLAY_RENDER_HEIGHT_PX - SHAPE_HEIGHT_PX * BIG_SCALE) / 2U;
  for (uint32_t i = 0; i < character_count; i++) {
    draw_character(text[i], left_px + i * step_px, top_px, BIG_SCALE, pixels);
  }
}

static void draw_small(const char *text, uint32_t character_count,
                       uint8_t *pixels) {
  const uint32_t max_characters = SMALL_CHARACTERS_PER_LINE * SMALL_MAX_LINES;
  const uint32_t drawn_count =
      character_count < max_characters ? character_count : max_characters;
  const uint32_t line_count = (drawn_count + SMALL_CHARACTERS_PER_LINE - 1U) /
                              SMALL_CHARACTERS_PER_LINE;
  const uint32_t block_height_px =
      (line_count - 1U) * SMALL_LINE_STEP_PX + SHAPE_HEIGHT_PX;
  const uint32_t top_px = (DISPLAY_RENDER_HEIGHT_PX - block_height_px) / 2U;
  const uint32_t left_px =
      (DISPLAY_RENDER_WIDTH_PX - (SMALL_CHARACTERS_PER_LINE * SMALL_STEP_PX)) /
      2U;
  for (uint32_t i = 0; i < drawn_count; i++) {
    const uint32_t column = i % SMALL_CHARACTERS_PER_LINE;
    const uint32_t line = i / SMALL_CHARACTERS_PER_LINE;
    draw_character(text[i], left_px + column * SMALL_STEP_PX,
                   top_px + line * SMALL_LINE_STEP_PX, SMALL_SCALE, pixels);
  }
}

const char *display_render_word_text(display_word_t word) {
  return word < DISPLAY_WORD_COUNT ? WORD_TEXTS[word] : "";
}

bool display_render_has_shape(char character) {
  return find_shape(character) != NULL;
}

void display_render_text(const char *text, uint8_t *pixels) {
  memset(pixels, 0, DISPLAY_RENDER_BUFFER_BYTES);
  const uint32_t character_count = (uint32_t)strlen(text);
  if (character_count == 0U) {
    return;
  }
  if (character_count <= BIG_MAX_CHARACTERS) {
    draw_big(text, character_count, pixels);
  } else {
    draw_small(text, character_count, pixels);
  }
}
