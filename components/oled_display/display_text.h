/**
 * @file display_text.h
 * @brief Draws lines of text into a 64x48 SSD1306 frame buffer.
 *
 * Plain C with no ESP-IDF includes so it runs in host tests. Frame layout
 * is the SSD1306 page format: one byte is 8 pixels in a column, bit 0 on
 * top; byte index = page * DISPLAY_WIDTH_PX + x.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_WIDTH_PX 64
#define DISPLAY_HEIGHT_PX 48
#define DISPLAY_FRAME_BYTES (DISPLAY_WIDTH_PX * DISPLAY_HEIGHT_PX / 8)
/* 5x7 font in a 6x8 cell: one text line per 8-pixel page. */
#define DISPLAY_LINE_COUNT (DISPLAY_HEIGHT_PX / 8)
#define DISPLAY_LINE_CHARS (DISPLAY_WIDTH_PX / 6)

/**
 * Clear the frame and draw each line centered on its own row. Text after
 * DISPLAY_LINE_CHARS or a '\0' is not drawn; characters outside printable
 * ASCII are drawn as '?'.
 */
void display_text_render(
    const char lines[DISPLAY_LINE_COUNT][DISPLAY_LINE_CHARS + 1],
    uint8_t frame[DISPLAY_FRAME_BYTES]);

#ifdef __cplusplus
}
#endif
