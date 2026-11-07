#include "oled_display.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_events.h"
#include "display_text.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "oled_display";

#define SDA_GPIO CONFIG_OLED_DISPLAY_SDA_GPIO
#define SCL_GPIO CONFIG_OLED_DISPLAY_SCL_GPIO
#define RESET_GPIO CONFIG_OLED_DISPLAY_RESET_GPIO
/* ESP32-S3: 22-25 do not exist, 26-37 are flash and octal PSRAM. */
#if (SDA_GPIO >= 22 && SDA_GPIO <= 37) ||                                      \
    (SCL_GPIO >= 22 && SCL_GPIO <= 37) ||                                      \
    (RESET_GPIO >= 22 && RESET_GPIO <= 37)
#error "An OLED display GPIO is not usable on ESP32-S3 (22-37)"
#endif

/* The camera's SCCB (esp32-camera) takes I2C1 by default. */
#define I2C_PORT 0
#if CONFIG_SCCB_HARDWARE_I2C_PORT0
#error "Camera SCCB is on I2C0, which the OLED display uses: set it to I2C1"
#endif
#define I2C_SPEED_HZ (400 * 1000)
/* The SSD1306 RAM is 128x64; a 64x48 panel shows columns 32-95. */
#define PANEL_COLUMN_OFFSET 32
#define SSD1306_CMD_SET_MULTIPLEX 0xA8
#define PROBE_TIMEOUT_MS 50
#define SHOW_RESULT_MS 2000
#define TASK_STACK_SIZE 3072
#define TASK_PRIORITY 2

typedef struct {
  char lines[DISPLAY_LINE_COUNT][DISPLAY_LINE_CHARS + 1];
  /* 0 = keep until the next screen; else go back to the idle screen. */
  uint32_t show_ms;
  /* Idle screen to go back to. Written by the event handler only. */
  bool is_door_open;
} screen_t;

/* Holds only the newest screen: a new event replaces the old one. */
static QueueHandle_t s_screens;
/* Used only by the display task after start. */
static esp_lcd_panel_handle_t s_panel;

/* Errors are logged only: a blank screen must never stop anything. */
static void draw(const screen_t *screen) {
  static uint8_t frame[DISPLAY_FRAME_BYTES];
  display_text_render(screen->lines, frame);
  esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DISPLAY_WIDTH_PX,
                                            DISPLAY_HEIGHT_PX, frame);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Draw failed: %s", esp_err_to_name(err));
  }
}

/* Lines are cut to fit; a NULL line stays blank. */
static screen_t make_screen(uint32_t show_ms, bool is_door_open,
                            const char *line_1, const char *line_2,
                            const char *line_3, const char *line_4) {
  screen_t screen = {.show_ms = show_ms, .is_door_open = is_door_open};
  const char *texts[] = {line_1, line_2, line_3, line_4};
  /* Rows 1-4 of 0-5: keeps the text off the panel edges. */
  for (int i = 0; i < 4; i++) {
    if (texts[i] != NULL) {
      snprintf(screen.lines[i + 1], sizeof(screen.lines[i + 1]), "%s",
               texts[i]);
    }
  }
  return screen;
}

static screen_t idle_screen(bool is_door_open) {
  return is_door_open
             ? make_screen(0, true, "OPEN", NULL, NULL, NULL)
             : make_screen(0, false, "LOCKED", NULL, "Look at", "camera");
}

static void on_app_event(void *handler_argument, esp_event_base_t base,
                         int32_t event_id, void *event_data) {
  (void)handler_argument;
  (void)base;
  /* Only the event loop task runs this handler, so no lock is needed. */
  static bool s_is_door_open;
  const app_event_face_t *face = event_data;
  const char *name = face != NULL ? face->name : "";
  screen_t screen;
  switch ((app_event_id_t)event_id) {
  case APP_EVENT_DOOR_OPENED:
    s_is_door_open = true;
    screen =
        make_screen(0, true, "OPEN", NULL, "Welcome",
                    face != NULL && face->face_id == 0 ? "(manual)" : name);
    break;
  case APP_EVENT_DOOR_CLOSED:
    s_is_door_open = false;
    screen = idle_screen(false);
    break;
  case APP_EVENT_FACE_REJECTED:
    screen = make_screen(SHOW_RESULT_MS, s_is_door_open, "Unknown", "face",
                         NULL, NULL);
    break;
  case APP_EVENT_APPROVAL_REQUESTED:
    screen =
        make_screen(0, s_is_door_open, name, NULL, "Waiting for", "approval");
    break;
  case APP_EVENT_APPROVAL_DENIED:
    screen =
        make_screen(SHOW_RESULT_MS, s_is_door_open, "Denied", NULL, NULL, NULL);
    break;
  case APP_EVENT_APPROVAL_TIMEOUT:
    screen = make_screen(SHOW_RESULT_MS, s_is_door_open, "No answer", NULL,
                         NULL, NULL);
    break;
  case APP_EVENT_ENROLL_STARTED:
    screen =
        make_screen(0, s_is_door_open, "Enroll", name, "Look at", "camera");
    break;
  case APP_EVENT_FACE_ENROLLED:
    screen =
        make_screen(SHOW_RESULT_MS, s_is_door_open, "Saved", name, NULL, NULL);
    break;
  case APP_EVENT_ENROLL_FAILED:
    screen = make_screen(SHOW_RESULT_MS, s_is_door_open, "Enroll", "failed",
                         NULL, NULL);
    break;
  default:
    return;
  }
  /* Never blocks: this runs on the shared event loop task. */
  xQueueOverwrite(s_screens, &screen);
}

static esp_err_t start_panel(void) {
  const i2c_master_bus_config_t bus_config = {
      .i2c_port = I2C_PORT,
      .sda_io_num = SDA_GPIO,
      .scl_io_num = SCL_GPIO,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      /* Weak; most OLED modules carry their own pull-ups. */
      .flags.enable_internal_pullup = true,
  };
  i2c_master_bus_handle_t bus = NULL;
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &bus), TAG,
                      "i2c bus failed");
  const esp_err_t probe_err =
      i2c_master_probe(bus, CONFIG_OLED_DISPLAY_I2C_ADDRESS, PROBE_TIMEOUT_MS);
  ESP_RETURN_ON_FALSE(probe_err != ESP_ERR_TIMEOUT, probe_err, TAG,
                      "I2C bus stuck: check 3V3/GND to the screen and wiring");
  ESP_RETURN_ON_FALSE(probe_err == ESP_OK, probe_err, TAG,
                      "no answer at 0x%02X: check wiring, try swapping SDA/SCL",
                      CONFIG_OLED_DISPLAY_I2C_ADDRESS);

  /* SSD1306 I2C control byte: bit 6 selects data (1) or command (0). */
  const esp_lcd_panel_io_i2c_config_t io_config = {
      .dev_addr = CONFIG_OLED_DISPLAY_I2C_ADDRESS,
      .scl_speed_hz = I2C_SPEED_HZ,
      .control_phase_bytes = 1,
      .dc_bit_offset = 6,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
  };
  esp_lcd_panel_io_handle_t io = NULL;
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_config, &io), TAG,
                      "panel io failed");

  /* The driver knows only 128x64 and 128x32: start as 64 rows, then set
     the multiplex ratio to 48 rows below. */
  esp_lcd_panel_ssd1306_config_t ssd1306_config = {.height = 64};
  const esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = RESET_GPIO, /* active low: flags left at 0 */
      .bits_per_pixel = 1,
      .vendor_config = &ssd1306_config,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ssd1306(io, &panel_config, &s_panel),
                      TAG, "panel failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
  ESP_RETURN_ON_ERROR(
      esp_lcd_panel_io_tx_param(io, SSD1306_CMD_SET_MULTIPLEX,
                                (uint8_t[]){DISPLAY_HEIGHT_PX - 1}, 1),
      TAG, "multiplex");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, PANEL_COLUMN_OFFSET, 0),
                      TAG, "gap");
#if CONFIG_OLED_DISPLAY_ROTATE_180
  ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, true), TAG, "rotate");
#endif
  return esp_lcd_panel_disp_on_off(s_panel, true);
}

static void display_task(void *argument) {
  (void)argument;
  /* Set up here, not in app_main: esp_lcd I2C writes wait with no timeout,
     so a bad screen may stall this task but never the lock app. */
  if (start_panel() != ESP_OK) {
    ESP_LOGE(TAG, "Display not running; everything else works");
    vTaskDelete(NULL);
  }
  ESP_LOGI(TAG, "SSD1306 64x48 at 0x%02X, SDA %d, SCL %d, reset %d",
           CONFIG_OLED_DISPLAY_I2C_ADDRESS, SDA_GPIO, SCL_GPIO, RESET_GPIO);
  screen_t screen = idle_screen(false);
  for (;;) {
    draw(&screen);
    const TickType_t wait_ticks =
        screen.show_ms > 0 ? pdMS_TO_TICKS(screen.show_ms) : portMAX_DELAY;
    if (xQueueReceive(s_screens, &screen, wait_ticks) != pdTRUE) {
      screen = idle_screen(screen.is_door_open);
    }
  }
}

esp_err_t oled_display_start(void) {
  if (SDA_GPIO < 0 || SCL_GPIO < 0) {
    ESP_LOGI(TAG, "Off (no SDA/SCL GPIO in menuconfig)");
    return ESP_OK;
  }
  ESP_RETURN_ON_FALSE(s_screens == NULL, ESP_ERR_INVALID_STATE, TAG,
                      "already started");

  s_screens = xQueueCreate(1, sizeof(screen_t));
  ESP_RETURN_ON_FALSE(s_screens != NULL, ESP_ERR_NO_MEM, TAG, "no queue");
  ESP_RETURN_ON_FALSE(xTaskCreate(display_task, "oled_display", TASK_STACK_SIZE,
                                  NULL, TASK_PRIORITY, NULL) == pdPASS,
                      ESP_ERR_NO_MEM, TAG, "no task");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(APP_EVENTS, ESP_EVENT_ANY_ID,
                                                 on_app_event, NULL),
                      TAG, "event register failed");
  return ESP_OK;
}
