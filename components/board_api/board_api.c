#include "board_api.h"

#include <stdbool.h>

#include "api_token_store.h"
#include "cJSON.h"
#include "door_control.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "net_comm.h"
#include "system_logic.h"

static const char *TAG = "board_api";

/* The stream server (port 81) keeps the default ctrl port; this one needs
   its own. Sockets: 4 here + 2 internal, see CONFIG_LWIP_MAX_SOCKETS. */
#define CONTROL_PORT 32770
#define MAX_OPEN_SOCKETS 4
#define STACK_SIZE_BYTES 6144

/* Set once at start, before the server runs. */
static face_db_t *s_face_db;

static esp_err_t send_json(httpd_req_t *request, cJSON *body) {
  char *text = cJSON_PrintUnformatted(body);
  cJSON_Delete(body);
  if (text == NULL) {
    return httpd_resp_send_500(request);
  }
  httpd_resp_set_type(request, "application/json");
  const esp_err_t err = httpd_resp_sendstr(request, text);
  cJSON_free(text);
  return err;
}

static esp_err_t handle_status(httpd_req_t *request) {
  if (!api_token_store_request_is_allowed(request)) {
    return api_token_store_send_unauthorized(request);
  }
  cJSON *body = cJSON_CreateObject();
  if (body == NULL) {
    return httpd_resp_send_500(request);
  }
  cJSON_AddStringToObject(body, "version", esp_app_get_description()->version);

  system_logic_status_t status;
  const bool is_recognition_running =
      system_logic_get_status(&status) == ESP_OK;
  cJSON_AddBoolToObject(body, "recognition", is_recognition_running);
  cJSON_AddStringToObject(
      body, "state",
      is_recognition_running ? unlock_state_name(status.state) : "OFF");
  cJSON_AddNumberToObject(body, "waiting_request",
                          is_recognition_running ? status.waiting_request_id
                                                 : 0);
  cJSON_AddStringToObject(body, "door",
                          door_control_is_open() ? "open" : "locked");
  if (s_face_db != NULL) {
    cJSON_AddNumberToObject(body, "faces", (double)face_db_count(s_face_db));
    cJSON_AddNumberToObject(body, "faces_max",
                            (double)face_db_capacity(s_face_db));
  } else {
    cJSON_AddNullToObject(body, "faces");
    cJSON_AddNullToObject(body, "faces_max");
  }
  char ip[NET_COMM_IP_MAX_LEN];
  net_comm_get_ip(ip);
  cJSON_AddStringToObject(body, "ip", ip);
  return send_json(request, body);
}

esp_err_t board_api_start(face_db_t *face_db) {
  if (CONFIG_NET_COMM_WIFI_SSID[0] == '\0') {
    ESP_LOGI(TAG, "Not started: Wi-Fi is off (menuconfig: Network)");
    return ESP_OK;
  }
  s_face_db = face_db;
  ESP_RETURN_ON_ERROR(api_token_store_init(), TAG, "token");
  char token[API_TOKEN_LEN + 1];
  api_token_store_get(token);
  /* Serial log only (needs the board in hand); never the OLED. */
  ESP_LOGI(TAG, "App token: %s (console: token, token new)", token);

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = BOARD_API_PORT;
  config.ctrl_port = CONTROL_PORT;
  config.core_id = 0; /* recognition owns core 1 */
  config.max_open_sockets = MAX_OPEN_SOCKETS;
  config.stack_size = STACK_SIZE_BYTES;
  config.lru_purge_enable = true;

  httpd_handle_t server = NULL;
  ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "http server");
  const httpd_uri_t status_route = {
      .uri = "/api/status", .method = HTTP_GET, .handler = handle_status};
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &status_route), TAG,
                      "route %s", status_route.uri);
  ESP_LOGI(TAG, "App API on port %d", config.server_port);
  return ESP_OK;
}
