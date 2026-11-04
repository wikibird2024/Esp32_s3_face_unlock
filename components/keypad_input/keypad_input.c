#include "keypad_input.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"

/* Set by any task, read and reset by the keypad task. */
static atomic_bool s_should_clear_lockout;

void keypad_input_clear_lockout(void) {
  atomic_store(&s_should_clear_lockout, true);
}

#if !CONFIG_KEYPAD_ENABLED

esp_err_t keypad_input_start(void) { return ESP_OK; }

#else

#include "app_events.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "key_debounce.h"
#include "pin_entry.h"
#include "pin_store.h"
#include "system_logic.h"

static const char *TAG = "keypad";

#define ROW1 CONFIG_KEYPAD_ROW1_GPIO
#define ROW2 CONFIG_KEYPAD_ROW2_GPIO
#define ROW3 CONFIG_KEYPAD_ROW3_GPIO
#define ROW4 CONFIG_KEYPAD_ROW4_GPIO
#define COLUMN1 CONFIG_KEYPAD_COLUMN1_GPIO
#define COLUMN2 CONFIG_KEYPAD_COLUMN2_GPIO
#define COLUMN3 CONFIG_KEYPAD_COLUMN3_GPIO
#define COLUMN4 CONFIG_KEYPAD_COLUMN4_GPIO

/* ESP32-S3: 22-25 do not exist, 26-37 are flash and octal PSRAM. */
#define IS_UNUSABLE_GPIO(gpio) ((gpio) >= 22 && (gpio) <= 37)
#if IS_UNUSABLE_GPIO(ROW1) || IS_UNUSABLE_GPIO(ROW2) ||                        \
    IS_UNUSABLE_GPIO(ROW3) || IS_UNUSABLE_GPIO(ROW4) ||                        \
    IS_UNUSABLE_GPIO(COLUMN1) || IS_UNUSABLE_GPIO(COLUMN2) ||                  \
    IS_UNUSABLE_GPIO(COLUMN3) || IS_UNUSABLE_GPIO(COLUMN4)
#error "A keypad GPIO is not usable on ESP32-S3 (22-37)"
#endif

#define IS_KEYPAD_GPIO(gpio)                                                   \
  ((gpio) == ROW1 || (gpio) == ROW2 || (gpio) == ROW3 || (gpio) == ROW4 ||     \
   (gpio) == COLUMN1 || (gpio) == COLUMN2 || (gpio) == COLUMN3 ||              \
   (gpio) == COLUMN4)
/* A row pulses low all the time, so on the lock pin it would move the lock. */
#if IS_KEYPAD_GPIO(CONFIG_DOOR_LOCK_GPIO)
#error "A keypad GPIO is the door lock GPIO"
#endif
#if IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_XCLK) ||                                  \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_SIOD) ||                                  \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_SIOC) ||                                  \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_D0) ||                                    \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_D1) ||                                    \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_D2) ||                                    \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_D3) ||                                    \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_D4) ||                                    \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_D5) ||                                    \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_D6) ||                                    \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_D7) ||                                    \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_VSYNC) ||                                 \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_HREF) ||                                  \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_PCLK) ||                                  \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_PWDN) ||                                  \
    IS_KEYPAD_GPIO(CONFIG_CAMERA_PIN_RESET)
#error "A keypad GPIO is also a camera GPIO"
#endif
#if IS_KEYPAD_GPIO(CONFIG_DISPLAY_SDA_GPIO) ||                                 \
    IS_KEYPAD_GPIO(CONFIG_DISPLAY_SCL_GPIO) ||                                 \
    IS_KEYPAD_GPIO(CONFIG_DISPLAY_RESET_GPIO)
#error "A keypad GPIO is also a display GPIO"
#endif
#if IS_KEYPAD_GPIO(CONFIG_FEEDBACK_BUZZER_GPIO) ||                             \
    IS_KEYPAD_GPIO(CONFIG_FEEDBACK_RGB_LED_GPIO)
#error "A keypad GPIO is also the buzzer or RGB LED GPIO"
#endif

_Static_assert(PIN_ENTRY_MIN_DIGITS == PIN_STORE_MIN_DIGITS &&
                   PIN_ENTRY_MAX_DIGITS == PIN_STORE_MAX_DIGITS,
               "A PIN that can be typed must also be one that can be set");

#define ROW_COUNT 4U
#define COLUMN_COUNT 4U
#define SCAN_PERIOD_MS 10U
/* Time for a column to rise again through its weak pull-up. */
#define SETTLE_TIME_US 20U
/* Large because the PIN hash runs on this stack. */
#define TASK_STACK_SIZE 6144U
#define TASK_PRIORITY 2U

static const gpio_num_t ROW_GPIOS[ROW_COUNT] = {ROW1, ROW2, ROW3, ROW4};
static const gpio_num_t COLUMN_GPIOS[COLUMN_COUNT] = {COLUMN1, COLUMN2, COLUMN3,
                                                      COLUMN4};
static const char KEY_NAMES[ROW_COUNT][COLUMN_COUNT] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'},
};

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* Returns the key held down, or KEY_DEBOUNCE_NO_KEY. Several keys at once count
   as none: a matrix can show keys that are not pressed in that case. */
static char scan_keys(void) {
  char key = KEY_DEBOUNCE_NO_KEY;
  uint32_t pressed_count = 0U;
  for (uint32_t row = 0U; row < ROW_COUNT; row++) {
    gpio_set_level(ROW_GPIOS[row], 0);
    esp_rom_delay_us(SETTLE_TIME_US);
    for (uint32_t column = 0U; column < COLUMN_COUNT; column++) {
      if (gpio_get_level(COLUMN_GPIOS[column]) == 0) {
        key = KEY_NAMES[row][column];
        pressed_count++;
      }
    }
    gpio_set_level(ROW_GPIOS[row], 1);
    esp_rom_delay_us(SETTLE_TIME_US);
  }
  return pressed_count == 1U ? key : KEY_DEBOUNCE_NO_KEY;
}

static void post_app_event(app_event_id_t event_id) {
  const esp_err_t err = esp_event_post(APP_EVENTS, event_id, NULL, 0, 0);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Event %s dropped: %s", app_event_name(event_id),
             esp_err_to_name(err));
  }
}

static void post_progress(uint8_t digit_count) {
  const keypad_pin_progress_t progress = {.digit_count = digit_count};
  /* A lost update only leaves the screen a digit behind. */
  esp_event_post(KEYPAD_EVENTS, KEYPAD_EVENT_PIN_PROGRESS, &progress,
                 sizeof(progress), 0);
}

static void handle_wrong_pin(const pin_entry_t *entry) {
  ESP_LOGW(TAG, "Wrong PIN (%lu in a row)", (unsigned long)entry->failed_count);
  post_app_event(APP_EVENT_PIN_REJECTED);
  if (entry->is_locked) {
    ESP_LOGW(
        TAG, "Keypad locked for %lu s",
        (unsigned long)(pin_entry_lockout_ms(entry->failed_count) / 1000U));
    post_app_event(APP_EVENT_PIN_LOCKED_OUT);
  }
}

static void check_pin(pin_entry_t *entry) {
  if (!pin_store_has_pin()) {
    ESP_LOGW(TAG, "No PIN set yet. In the console: pin set <digits>");
    pin_entry_forget_digits(entry);
    post_app_event(APP_EVENT_PIN_REJECTED);
    return;
  }
  /* Counted before checking, so cutting the power during the check does not
     hide a wrong try. Without a saved count the PIN is refused. */
  if (pin_store_save_failed_count(entry->failed_count + 1U) != ESP_OK) {
    ESP_LOGE(TAG, "Could not save the try count: PIN refused");
    pin_entry_forget_digits(entry);
    post_app_event(APP_EVENT_PIN_REJECTED);
    return;
  }
  const bool is_correct = pin_store_check(entry->digits);
  pin_entry_finish(entry, is_correct, now_ms());
  if (!is_correct) {
    handle_wrong_pin(entry);
    return;
  }

  ESP_LOGI(TAG, "PIN accepted");
  if (pin_store_save_failed_count(0U) != ESP_OK) {
    ESP_LOGW(TAG, "Could not reset the try count");
  }
  const esp_err_t err = system_logic_request_unlock();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Could not ask for the door to open: %s",
             esp_err_to_name(err));
    post_app_event(APP_EVENT_PIN_REJECTED);
  }
}

static void carry_out(pin_entry_action_t action, pin_entry_t *entry) {
  switch (action) {
  case PIN_ENTRY_ACTION_NONE:
    break;
  case PIN_ENTRY_ACTION_PROGRESS:
    post_progress(entry->digit_count);
    break;
  case PIN_ENTRY_ACTION_CHECK:
    check_pin(entry);
    break;
  case PIN_ENTRY_ACTION_REJECT:
    post_app_event(APP_EVENT_PIN_REJECTED);
    break;
  case PIN_ENTRY_ACTION_LOCKED_OUT:
    post_app_event(APP_EVENT_PIN_LOCKED_OUT);
    break;
  }
}

static void keypad_task(void *argument) {
  (void)argument;
  key_debounce_t debounce;
  key_debounce_init(&debounce);
  pin_entry_t entry;
  pin_entry_init(&entry, pin_store_failed_count(), now_ms());
  if (entry.is_locked) {
    ESP_LOGW(TAG, "Keypad locked after a restart (%lu wrong PINs in a row)",
             (unsigned long)entry.failed_count);
  }

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
    if (atomic_exchange(&s_should_clear_lockout, false)) {
      pin_entry_clear_lockout(&entry);
    }
    const char key = key_debounce_update(&debounce, scan_keys());
    const uint32_t time_ms = now_ms();
    carry_out(key == KEY_DEBOUNCE_NO_KEY ? pin_entry_tick(&entry, time_ms)
                                         : pin_entry_key(&entry, key, time_ms),
              &entry);
  }
}

esp_err_t keypad_input_start(void) {
  /* Open drain: if two keys of a column are pressed, two rows are joined, and
     a row driving high against a row driving low would be a short. */
  gpio_config_t io_config = {
      .mode = GPIO_MODE_OUTPUT_OD,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  for (uint32_t row = 0U; row < ROW_COUNT; row++) {
    io_config.pin_bit_mask |= 1ULL << ROW_GPIOS[row];
  }
  ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "row gpio failed");
  for (uint32_t row = 0U; row < ROW_COUNT; row++) {
    gpio_set_level(ROW_GPIOS[row], 1);
  }

  io_config.mode = GPIO_MODE_INPUT;
  io_config.pin_bit_mask = 0ULL;
  for (uint32_t column = 0U; column < COLUMN_COUNT; column++) {
    io_config.pin_bit_mask |= 1ULL << COLUMN_GPIOS[column];
  }
  ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "column gpio failed");

  ESP_RETURN_ON_FALSE(xTaskCreate(keypad_task, "keypad", TASK_STACK_SIZE, NULL,
                                  TASK_PRIORITY, NULL) == pdPASS,
                      ESP_ERR_NO_MEM, TAG, "no task");
  ESP_LOGI(TAG, "Rows GPIO %d %d %d %d, columns GPIO %d %d %d %d. PIN %s", ROW1,
           ROW2, ROW3, ROW4, COLUMN1, COLUMN2, COLUMN3, COLUMN4,
           pin_store_has_pin() ? "set" : "NOT set (pin set <digits>)");
  return ESP_OK;
}

#endif
