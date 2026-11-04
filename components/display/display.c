#include "display.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_events.h"
#include "display_render.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "display";

#define SDA_GPIO CONFIG_DISPLAY_SDA_GPIO
#define SCL_GPIO CONFIG_DISPLAY_SCL_GPIO
#define RESET_GPIO CONFIG_DISPLAY_RESET_GPIO
/* ESP32-S3: 22-25 do not exist, 26-37 are flash and octal PSRAM. */
#if (SDA_GPIO >= 22 && SDA_GPIO <= 37) ||                                      \
    (SCL_GPIO >= 22 && SCL_GPIO <= 37) ||                                      \
    (RESET_GPIO >= 22 && RESET_GPIO <= 37)
#error "A display GPIO is not usable on ESP32-S3 (22-37)"
#endif
/* I2C traffic on the lock pin would move the lock. */
#if SDA_GPIO == CONFIG_DOOR_LOCK_GPIO || SCL_GPIO == CONFIG_DOOR_LOCK_GPIO ||  \
    RESET_GPIO == CONFIG_DOOR_LOCK_GPIO
#error "A display GPIO is the door lock GPIO"
#endif
/* The OV5640 has the same I2C address as the screen, so screen commands would
   reach the camera. */
#if SDA_GPIO == CONFIG_CAMERA_PIN_SIOD ||                                      \
    SDA_GPIO == CONFIG_CAMERA_PIN_SIOC ||                                      \
    SCL_GPIO == CONFIG_CAMERA_PIN_SIOD || SCL_GPIO == CONFIG_CAMERA_PIN_SIOC
#error "A display GPIO is on the camera bus"
#endif

/* The camera driver picks its I2C port in menuconfig; take the other one. */
#if CONFIG_SCCB_HARDWARE_I2C_PORT1
#define I2C_PORT I2C_NUM_0
#else
#define I2C_PORT I2C_NUM_1
#endif

/* Slow on purpose: a word only changes on an event, a full screen still takes
   about 40 ms, and it is kinder to weak pull-ups and loose wires. */
#define I2C_CLOCK_HZ 100000U
#define I2C_GLITCH_FILTER_CYCLES 7U
#define SSD1306_COMMAND_BITS 8
#define SSD1306_CONTROL_BYTES 1U
#define SSD1306_DATA_COMMAND_BIT 6U
#define SSD1306_SET_ROW_COUNT 0xA8
/* The driver only sets up the row wiring this panel needs when it is told 64
   rows. The real row count is sent after its init. */
#define DRIVER_HEIGHT_FOR_ROW_WIRING 64U
/* The 64 columns of this panel sit in the middle of the chip's 128. */
#define PANEL_COLUMN_OFFSET 32

#if CONFIG_DISPLAY_UPSIDE_DOWN
#define IS_MIRRORED false
#else
#define IS_MIRRORED true
#endif

#define SHOW_RESULT_MS 2000U
#define SHOW_IP_ADDRESS_MS 10000U
/* Longer than the time the keypad keeps typed digits. */
#define SHOW_PIN_ENTRY_MS 15000U
/* Longer than the longest door hold, approval wait or enroll in menuconfig.
   If an event gets lost, a word such as OPEN does not stay on forever. */
#define SHOW_UNTIL_NEXT_EVENT_MS 130000U
#define TASK_STACK_SIZE 4096U
#define RETRY_TURN_OFF_MS 1000U
#define TASK_PRIORITY 2U
/* Long enough for "255.255.255.255". */
#define SCREEN_TEXT_MAX_BYTES 16U
#define PIN_STARS "****"
#define PIN_STARS_COUNT 4U

/* Empty text means the screen should go off. */
typedef struct {
  char text[SCREEN_TEXT_MAX_BYTES];
  uint32_t show_ms;
} screen_t;

/* Holds only the newest screen: a new event replaces an old one. */
static QueueHandle_t s_screens;
/* Used only by the display task. */
static uint8_t s_pixels[DISPLAY_RENDER_BUFFER_BYTES];

/* Never blocks: this runs on the shared event loop task. */
static void show_text(const char *text, uint32_t show_ms) {
  screen_t screen = {.show_ms = show_ms};
  snprintf(screen.text, sizeof(screen.text), "%s", text);
  xQueueOverwrite(s_screens, &screen);
}

static void show_word(display_word_t word, uint32_t show_ms) {
  show_text(display_render_word_text(word), show_ms);
}

static void on_app_event(void *handler_argument, esp_event_base_t base,
                         int32_t event_id, void *event_data) {
  (void)handler_argument;
  (void)base;
  (void)event_data;
  switch ((app_event_id_t)event_id) {
  case APP_EVENT_DOOR_OPENED:
    show_word(DISPLAY_WORD_OPEN, SHOW_UNTIL_NEXT_EVENT_MS);
    break;
  case APP_EVENT_DOOR_CLOSED:
    show_text("", 0U);
    break;
  case APP_EVENT_APPROVAL_REQUESTED:
    show_word(DISPLAY_WORD_WAIT, SHOW_UNTIL_NEXT_EVENT_MS);
    break;
  case APP_EVENT_FACE_REJECTED:
  case APP_EVENT_APPROVAL_DENIED:
  case APP_EVENT_PIN_REJECTED:
    show_word(DISPLAY_WORD_DENY, SHOW_RESULT_MS);
    break;
  case APP_EVENT_APPROVAL_TIMEOUT:
    show_word(DISPLAY_WORD_TIME, SHOW_RESULT_MS);
    break;
  case APP_EVENT_ENROLL_STARTED:
    show_word(DISPLAY_WORD_SCAN, SHOW_UNTIL_NEXT_EVENT_MS);
    break;
  case APP_EVENT_FACE_ENROLLED:
    show_word(DISPLAY_WORD_DONE, SHOW_RESULT_MS);
    break;
  case APP_EVENT_ENROLL_FAILED:
    show_word(DISPLAY_WORD_FAIL, SHOW_RESULT_MS);
    break;
  case APP_EVENT_PIN_LOCKED_OUT:
    show_word(DISPLAY_WORD_LOCK, SHOW_RESULT_MS);
    break;
  case APP_EVENT_RECOGNITION_FAILED:
    show_word(DISPLAY_WORD_ERROR, SHOW_UNTIL_NEXT_EVENT_MS);
    break;
  }
}

static void on_keypad_event(void *handler_argument, esp_event_base_t base,
                            int32_t event_id, void *event_data) {
  (void)handler_argument;
  (void)base;
  (void)event_id;
  const keypad_pin_progress_t *progress = event_data;
  if (progress->digit_count == 0U) {
    show_text("", 0U);
    return;
  }
  /* One star per digit, so longer PINs simply stop at 4 stars. */
  const uint32_t star_count = progress->digit_count < PIN_STARS_COUNT
                                  ? progress->digit_count
                                  : PIN_STARS_COUNT;
  show_text(PIN_STARS + (PIN_STARS_COUNT - star_count), SHOW_PIN_ENTRY_MS);
}

static void on_got_ip(void *handler_argument, esp_event_base_t base,
                      int32_t event_id, void *event_data) {
  (void)handler_argument;
  (void)base;
  (void)event_id;
  const ip_event_got_ip_t *event = event_data;
  char ip_address[SCREEN_TEXT_MAX_BYTES];
  snprintf(ip_address, sizeof(ip_address), IPSTR, IP2STR(&event->ip_info.ip));
  show_text(ip_address, SHOW_IP_ADDRESS_MS);
}

/* Not undone on failure: nothing retries, and the board works without it. */
static esp_err_t start_panel(esp_lcd_panel_handle_t *out_panel) {
  const i2c_master_bus_config_t bus_config = {
      .i2c_port = I2C_PORT,
      .sda_io_num = SDA_GPIO,
      .scl_io_num = SCL_GPIO,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = I2C_GLITCH_FILTER_CYCLES,
      .flags.enable_internal_pullup = true,
  };
  i2c_master_bus_handle_t bus = NULL;
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &bus), TAG, "i2c bus");

  const esp_lcd_panel_io_i2c_config_t io_config = {
      .dev_addr = CONFIG_DISPLAY_I2C_ADDRESS,
      .scl_speed_hz = I2C_CLOCK_HZ,
      .control_phase_bytes = SSD1306_CONTROL_BYTES,
      .dc_bit_offset = SSD1306_DATA_COMMAND_BIT,
      .lcd_cmd_bits = SSD1306_COMMAND_BITS,
      .lcd_param_bits = SSD1306_COMMAND_BITS,
  };
  esp_lcd_panel_io_handle_t panel_io = NULL;
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_config, &panel_io), TAG,
                      "panel io");

  esp_lcd_panel_ssd1306_config_t ssd1306_config = {
      .height = DRIVER_HEIGHT_FOR_ROW_WIRING,
  };
  const esp_lcd_panel_dev_config_t panel_config = {
      .bits_per_pixel = 1,
      .reset_gpio_num = RESET_GPIO,
      .vendor_config = &ssd1306_config,
  };
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_ssd1306(panel_io, &panel_config, out_panel), TAG,
      "panel");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*out_panel), TAG, "panel reset");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*out_panel), TAG,
                      "no answer from screen at 0x%02X",
                      CONFIG_DISPLAY_I2C_ADDRESS);

  const uint8_t last_row = DISPLAY_RENDER_HEIGHT_PX - 1U;
  ESP_RETURN_ON_ERROR(
      esp_lcd_panel_io_tx_param(panel_io, SSD1306_SET_ROW_COUNT, &last_row, 1U),
      TAG, "row count");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(*out_panel, PANEL_COLUMN_OFFSET, 0),
                      TAG, "column offset");
  return esp_lcd_panel_mirror(*out_panel, IS_MIRRORED, IS_MIRRORED);
}

/* False when the screen may still be on. */
static bool turn_off(esp_lcd_panel_handle_t panel, bool *is_on) {
  if (*is_on && esp_lcd_panel_disp_on_off(panel, false) != ESP_OK) {
    ESP_LOGW(TAG, "Could not turn the screen off");
    return false;
  }
  *is_on = false;
  return true;
}

static void draw_text(esp_lcd_panel_handle_t panel, const char *text,
                      bool *is_on) {
  display_render_text(text, s_pixels);
  if (esp_lcd_panel_draw_bitmap(panel, 0, 0, DISPLAY_RENDER_WIDTH_PX,
                                DISPLAY_RENDER_HEIGHT_PX, s_pixels) != ESP_OK) {
    ESP_LOGW(TAG, "Could not draw the screen");
    return;
  }
  if (!*is_on && esp_lcd_panel_disp_on_off(panel, true) != ESP_OK) {
    ESP_LOGW(TAG, "Could not turn the screen on");
    return;
  }
  *is_on = true;
}

static void display_task(void *argument) {
  (void)argument;
  esp_lcd_panel_handle_t panel = NULL;
  if (start_panel(&panel) != ESP_OK) {
    ESP_LOGW(TAG, "No screen; everything else works");
    vTaskDelete(NULL);
  }
  ESP_LOGI(TAG, "Screen on SDA %d, SCL %d, address 0x%02X", SDA_GPIO, SCL_GPIO,
           CONFIG_DISPLAY_I2C_ADDRESS);

  bool is_on = false;
  TickType_t wait_ticks = portMAX_DELAY;
  for (;;) {
    screen_t screen;
    if (xQueueReceive(s_screens, &screen, wait_ticks) != pdTRUE ||
        screen.text[0] == '\0') {
      /* Off when idle, so the panel does not burn in. If that fails, try again:
         a word such as OPEN must not stay lit on a locked door. */
      wait_ticks = turn_off(panel, &is_on) ? portMAX_DELAY
                                           : pdMS_TO_TICKS(RETRY_TURN_OFF_MS);
      continue;
    }
    draw_text(panel, screen.text, &is_on);
    wait_ticks = pdMS_TO_TICKS(screen.show_ms);
  }
}

esp_err_t display_start(void) {
  if (SDA_GPIO < 0 || SCL_GPIO < 0) {
    ESP_LOGI(TAG, "Off (no SDA or SCL GPIO in menuconfig)");
    return ESP_OK;
  }
  s_screens = xQueueCreate(1, sizeof(screen_t));
  ESP_RETURN_ON_FALSE(s_screens != NULL, ESP_ERR_NO_MEM, TAG, "no queue");
  /* Listening starts here, not in the task: the queue keeps the newest screen
     until the panel is ready, and events from early in the boot still show. */
  ESP_RETURN_ON_ERROR(esp_event_handler_register(APP_EVENTS, ESP_EVENT_ANY_ID,
                                                 on_app_event, NULL),
                      TAG, "event register failed");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(KEYPAD_EVENTS,
                                                 KEYPAD_EVENT_PIN_PROGRESS,
                                                 on_keypad_event, NULL),
                      TAG, "event register failed");
  /* With no SSID the compiler drops this, and the network code with it. */
  if (CONFIG_NET_COMM_WIFI_SSID[0] != '\0') {
    ESP_RETURN_ON_ERROR(esp_event_handler_register(
                            IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL),
                        TAG, "event register failed");
  }
  ESP_RETURN_ON_FALSE(xTaskCreate(display_task, "display", TASK_STACK_SIZE,
                                  NULL, TASK_PRIORITY, NULL) == pdPASS,
                      ESP_ERR_NO_MEM, TAG, "no task");
  return ESP_OK;
}
