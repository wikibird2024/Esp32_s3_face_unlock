#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_RENDER_WIDTH_PX 64U
#define DISPLAY_RENDER_HEIGHT_PX 48U
#define DISPLAY_RENDER_BUFFER_BYTES                                            \
  (DISPLAY_RENDER_WIDTH_PX * DISPLAY_RENDER_HEIGHT_PX / 8U)

/* Every status word the screen can show. Add new ones here: a host test checks
   that all their letters have a shape. */
typedef enum {
  DISPLAY_WORD_OPEN = 0,
  DISPLAY_WORD_DENY,
  DISPLAY_WORD_TIME,
  DISPLAY_WORD_WAIT,
  DISPLAY_WORD_SCAN,
  DISPLAY_WORD_DONE,
  DISPLAY_WORD_FAIL,
  DISPLAY_WORD_LOCK,
  DISPLAY_WORD_ERROR,
  DISPLAY_WORD_COUNT,
} display_word_t;

const char *display_render_word_text(display_word_t word);

/* True when this character has a shape in the font. */
bool display_render_has_shape(char character);

/* Clears pixels (DISPLAY_RENDER_BUFFER_BYTES long) and draws text in the middle.
   Up to 4 characters are drawn big. Longer text is drawn small and wraps every
   10 characters, up to 4 lines. A character with no shape stays blank.
   The layout is the SSD1306 one: 6 rows of 64 bytes, and each byte is one
   column of 8 pixels with the lowest bit on top. */
void display_render_text(const char *text, uint8_t *pixels);

#ifdef __cplusplus
}
#endif
