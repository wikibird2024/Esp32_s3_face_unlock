#include "ai_face_recognition.h"
#include "app_events.h"
#include "camera_driver.h"
#include "console_commands.h"
#include "display.h"
#include "door_control.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "face_db_nvs.h"
#include "feedback.h"
#include "keypad_input.h"
#include "net_comm.h"
#include "nvs_flash.h"
#include "system_logic.h"
#include "video_stream.h"

static const char *TAG = "app_main";

static esp_err_t init_default_nvs(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "Default NVS unusable (%s), erasing", esp_err_to_name(err));
    ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase failed");
    err = nvs_flash_init();
  }
  return err;
}

/* Model first, then face store (needs the embedding length), then camera. A
   broken camera only stops face unlock: system logic still starts, so the PIN
   and the console can open the door, and the console can list and delete faces.
   Returns the camera error in that case. */
static esp_err_t start_recognition(face_db_t **out_face_db) {
  ESP_RETURN_ON_ERROR(ai_face_recognition_init(), TAG, "face model");
  ESP_RETURN_ON_ERROR(
      face_db_nvs_open(ai_face_recognition_embedding_len(), out_face_db), TAG,
      "face store");
  const esp_err_t camera_err = camera_driver_init();
  ESP_RETURN_ON_ERROR(system_logic_start(*out_face_db), TAG, "system logic");
  return camera_err;
}

void app_main(void) {
  /* Lock output first: the door must be locked whatever fails later,
   * including NVS. Needs only GPIO and esp_timer, both ready by now. */
  ESP_ERROR_CHECK(door_control_init());

  const esp_app_desc_t *app = esp_app_get_description();
  ESP_LOGI(TAG, "Face unlock %s (IDF %s)", app->version, app->idf_ver);

  ESP_ERROR_CHECK(init_default_nvs());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  /* Before Wi-Fi, so the screen gets the first IP address. */
  if (display_start() != ESP_OK) {
    ESP_LOGW(TAG, "No screen; everything else works");
  }
  ESP_ERROR_CHECK(net_comm_init());
  if (feedback_start() != ESP_OK) {
    ESP_LOGW(TAG, "No beep/LED feedback; everything else works");
  }

  face_db_t *face_db = NULL;
  esp_err_t err = start_recognition(&face_db);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Face unlock NOT running (%s). Door stays locked; console "
             "is still available for diagnosis.",
             esp_err_to_name(err));
    esp_event_post(APP_EVENTS, APP_EVENT_RECOGNITION_FAILED, NULL, 0, 0);
  }
  if (err == ESP_OK && video_stream_start() != ESP_OK) {
    ESP_LOGW(TAG, "Live view not running; everything else works");
  }
  if (keypad_input_start() != ESP_OK) {
    ESP_LOGW(TAG, "No keypad; everything else works");
  }

  ESP_ERROR_CHECK(console_commands_start(face_db));
  ESP_LOGI(TAG, "Ready. Type 'help' in the console.");
}
