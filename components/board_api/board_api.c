#include "board_api.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "api_token_store.h"
#include "app_events.h"
#include "cJSON.h"
#include "camera_driver.h"
#include "door_control.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "event_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "net_comm.h"
#include "system_logic.h"

static const char *TAG = "board_api";

/* The stream server (port 81) keeps the default ctrl port; this one needs
   its own. Sockets: 4 here + 2 internal, see CONFIG_LWIP_MAX_SOCKETS. */
#define CONTROL_PORT 32770
#define MAX_OPEN_SOCKETS 4
#define MAX_ROUTES 16
#define STACK_SIZE_BYTES 6144
#define MAX_BODY_BYTES 512
#define QUERY_MAX_LEN 32
#define FACES_PATH_PREFIX "/api/faces/"

/* Set once at start, before the server runs. */
static face_db_t *s_face_db;
/* Written by the event loop task, read by the server task. */
static event_log_t s_event_log;
static SemaphoreHandle_t s_event_log_mutex;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ---- Event log --------------------------------------------------------- */

static void on_app_event(void *handler_argument, esp_event_base_t base,
                         int32_t event_id, void *event_data) {
  (void)handler_argument;
  (void)base;
  event_log_entry_t entry = {.time_ms = now_ms(), .type = event_id};
  const app_event_face_t *face = event_data;
  if (face != NULL) {
    entry.face_id = face->face_id;
    entry.similarity = face->similarity;
    entry.request_id = face->request_id;
    strncpy(entry.name, face->name, EVENT_LOG_NAME_MAX_LEN);
  }
  xSemaphoreTake(s_event_log_mutex, portMAX_DELAY);
  event_log_add(&s_event_log, &entry);
  xSemaphoreGive(s_event_log_mutex);
}

/* Copies the log entries after after_seq; NULL when out of memory. The
   caller frees the result. */
static event_log_entry_t *copy_events(uint32_t after_seq, size_t *out_count,
                                      uint32_t *out_last_seq) {
  event_log_entry_t *entries =
      malloc(EVENT_LOG_CAPACITY * sizeof(event_log_entry_t));
  if (entries == NULL) {
    return NULL;
  }
  xSemaphoreTake(s_event_log_mutex, portMAX_DELAY);
  *out_count = event_log_read_after(&s_event_log, after_seq, entries,
                                    EVENT_LOG_CAPACITY);
  *out_last_seq = s_event_log.last_seq;
  xSemaphoreGive(s_event_log_mutex);
  return entries;
}

/* ---- HTTP helpers ------------------------------------------------------ */

static esp_err_t send_json(httpd_req_t *request, const char *status,
                           cJSON *body) {
  char *text = body != NULL ? cJSON_PrintUnformatted(body) : NULL;
  cJSON_Delete(body);
  if (text == NULL) {
    return httpd_resp_send_500(request);
  }
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "application/json");
  const esp_err_t err = httpd_resp_sendstr(request, text);
  cJSON_free(text);
  return err;
}

static esp_err_t send_error(httpd_req_t *request, const char *status,
                            const char *message) {
  cJSON *body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "error", message);
  return send_json(request, status, body);
}

static esp_err_t send_accepted(httpd_req_t *request) {
  cJSON *body = cJSON_CreateObject();
  cJSON_AddBoolToObject(body, "ok", true);
  return send_json(request, "202 Accepted", body);
}

/* Answers a refused request from system_logic or the camera. */
static esp_err_t send_request_error(httpd_req_t *request, esp_err_t err) {
  if (err == ESP_ERR_INVALID_ARG) {
    return send_error(request, "400 Bad Request", "value out of range");
  }
  if (err == ESP_ERR_INVALID_STATE) {
    return send_error(request, "503 Service Unavailable",
                      "recognition or camera not running");
  }
  return send_error(request, "503 Service Unavailable", esp_err_to_name(err));
}

/* True (and 401 sent) when the request has no valid token. */
static bool is_refused(httpd_req_t *request) {
  if (api_token_store_request_is_allowed(request)) {
    return false;
  }
  api_token_store_send_unauthorized(request);
  return true;
}

/* Reads a small JSON body; NULL when missing, too big or not JSON. */
static cJSON *read_json_body(httpd_req_t *request) {
  if (request->content_len == 0 || request->content_len > MAX_BODY_BYTES) {
    return NULL;
  }
  char text[MAX_BODY_BYTES + 1];
  size_t received_bytes = 0;
  while (received_bytes < request->content_len) {
    const int length = httpd_req_recv(request, text + received_bytes,
                                      request->content_len - received_bytes);
    if (length <= 0) {
      return NULL;
    }
    received_bytes += (size_t)length;
  }
  text[received_bytes] = '\0';
  return cJSON_Parse(text);
}

/* Whole number in [min, max] at `key`; false when missing or not one. */
static bool read_whole_number(const cJSON *body, const char *key, double min,
                              double max, double *out_value) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(body, key);
  if (!cJSON_IsNumber(item)) {
    return false;
  }
  const double value = item->valuedouble;
  if (value != floor(value) || value < min || value > max) {
    return false;
  }
  *out_value = value;
  return true;
}

/* ---- Status and events ------------------------------------------------- */

/* Name of the face waiting for approval and the time left, from the log. */
static void add_waiting_approval(cJSON *body, uint32_t request_id) {
  size_t count = 0;
  uint32_t last_seq = 0;
  event_log_entry_t *entries = copy_events(0, &count, &last_seq);
  const event_log_entry_t *asked = NULL;
  for (size_t i = 0; entries != NULL && i < count; i++) {
    if (entries[i].type == APP_EVENT_APPROVAL_REQUESTED &&
        entries[i].request_id == request_id) {
      asked = &entries[i];
    }
  }
  unlock_rules_t rules;
  if (asked != NULL && system_logic_get_rules(&rules) == ESP_OK) {
    const uint32_t waited_ms = now_ms() - asked->time_ms;
    const uint32_t left_ms = waited_ms < rules.approval_timeout_ms
                                 ? rules.approval_timeout_ms - waited_ms
                                 : 0;
    cJSON_AddStringToObject(body, "waiting_name", asked->name);
    cJSON_AddNumberToObject(body, "waiting_left_ms", left_ms);
  }
  free(entries);
}

static esp_err_t handle_status(httpd_req_t *request) {
  if (is_refused(request)) {
    return ESP_OK;
  }
  cJSON *body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "version", esp_app_get_description()->version);
  system_logic_status_t status;
  const bool is_recognition_running =
      system_logic_get_status(&status) == ESP_OK;
  cJSON_AddBoolToObject(body, "recognition", is_recognition_running);
  cJSON_AddStringToObject(
      body, "state",
      is_recognition_running ? unlock_state_name(status.state) : "OFF");
  const uint32_t waiting_request =
      is_recognition_running ? status.waiting_request_id : 0;
  cJSON_AddNumberToObject(body, "waiting_request", waiting_request);
  if (waiting_request != 0) {
    add_waiting_approval(body, waiting_request);
  }
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
  return send_json(request, "200 OK", body);
}

/* GET /api/events?after=<seq>: events newer than seq, oldest first. */
static esp_err_t handle_events(httpd_req_t *request) {
  if (is_refused(request)) {
    return ESP_OK;
  }
  uint32_t after_seq = 0;
  char query[QUERY_MAX_LEN];
  char value[16];
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK &&
      httpd_query_key_value(query, "after", value, sizeof(value)) == ESP_OK) {
    after_seq = (uint32_t)strtoul(value, NULL, 10);
  }
  size_t count = 0;
  uint32_t last_seq = 0;
  event_log_entry_t *entries = copy_events(after_seq, &count, &last_seq);
  if (entries == NULL) {
    return httpd_resp_send_500(request);
  }
  cJSON *body = cJSON_CreateObject();
  cJSON_AddNumberToObject(body, "now_ms", now_ms());
  cJSON_AddNumberToObject(body, "last_seq", last_seq);
  cJSON *list = cJSON_AddArrayToObject(body, "events");
  for (size_t i = 0; i < count; i++) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "seq", entries[i].seq);
    cJSON_AddNumberToObject(item, "time_ms", entries[i].time_ms);
    cJSON_AddStringToObject(item, "type", app_event_name(entries[i].type));
    cJSON_AddNumberToObject(item, "face_id", entries[i].face_id);
    cJSON_AddStringToObject(item, "name", entries[i].name);
    cJSON_AddNumberToObject(item, "similarity", entries[i].similarity);
    cJSON_AddNumberToObject(item, "request_id", entries[i].request_id);
    cJSON_AddItemToArray(list, item);
  }
  free(entries);
  return send_json(request, "200 OK", body);
}

/* ---- Faces ------------------------------------------------------------- */

static esp_err_t handle_faces_list(httpd_req_t *request) {
  if (is_refused(request)) {
    return ESP_OK;
  }
  if (s_face_db == NULL) {
    return send_request_error(request, ESP_ERR_INVALID_STATE);
  }
  cJSON *body = cJSON_CreateObject();
  cJSON_AddNumberToObject(body, "max", (double)face_db_capacity(s_face_db));
  cJSON *list = cJSON_AddArrayToObject(body, "faces");
  const size_t count = face_db_count(s_face_db);
  for (size_t i = 0; i < count; i++) {
    face_db_entry_t entry;
    if (face_db_get_entry(s_face_db, i, &entry) != FACE_DB_OK) {
      continue; /* deleted meanwhile */
    }
    cJSON *item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "id", entry.id);
    cJSON_AddStringToObject(item, "name", entry.name);
    cJSON_AddItemToArray(list, item);
  }
  return send_json(request, "200 OK", body);
}

/* Name rules: 1..FACE_DB_NAME_MAX_LEN bytes, no control characters
   (UTF-8 letters are fine; the OLED shows them as '?'). */
static bool is_valid_name(const char *name) {
  const size_t length_bytes = strlen(name);
  if (length_bytes == 0 || length_bytes > FACE_DB_NAME_MAX_LEN) {
    return false;
  }
  for (size_t i = 0; i < length_bytes; i++) {
    if ((unsigned char)name[i] < 0x20 || name[i] == 0x7F) {
      return false;
    }
  }
  return true;
}

/* POST /api/faces/enroll {"name": "..."}: the next face seen is saved. */
static esp_err_t handle_enroll(httpd_req_t *request) {
  if (is_refused(request)) {
    return ESP_OK;
  }
  cJSON *body = read_json_body(request);
  const cJSON *name = cJSON_GetObjectItemCaseSensitive(body, "name");
  esp_err_t err = ESP_ERR_INVALID_ARG;
  if (cJSON_IsString(name) && is_valid_name(name->valuestring)) {
    err = system_logic_request_enroll(name->valuestring);
  }
  cJSON_Delete(body);
  return err == ESP_OK ? send_accepted(request)
                       : send_request_error(request, err);
}

static esp_err_t handle_enroll_cancel(httpd_req_t *request) {
  if (is_refused(request)) {
    return ESP_OK;
  }
  const esp_err_t err = system_logic_request_cancel_enroll();
  return err == ESP_OK ? send_accepted(request)
                       : send_request_error(request, err);
}

/* DELETE /api/faces/<id> */
static esp_err_t handle_face_delete(httpd_req_t *request) {
  if (is_refused(request)) {
    return ESP_OK;
  }
  if (s_face_db == NULL) {
    return send_request_error(request, ESP_ERR_INVALID_STATE);
  }
  const char *id_text = request->uri + strlen(FACES_PATH_PREFIX);
  char *end = NULL;
  const unsigned long id = strtoul(id_text, &end, 10);
  if (end == id_text || (*end != '\0' && *end != '?') || id == 0 ||
      id > UINT16_MAX) {
    return send_error(request, "400 Bad Request", "not a face id");
  }
  const face_db_status_t status = face_db_delete(s_face_db, (uint16_t)id);
  if (status == FACE_DB_ERR_NOT_FOUND) {
    return send_error(request, "404 Not Found", "no face with that id");
  }
  if (status != FACE_DB_OK) {
    return send_error(request, "500 Internal Server Error",
                      face_db_status_name(status));
  }
  ESP_LOGI(TAG, "Face %lu deleted from the app", id);
  return send_accepted(request);
}

/* ---- Door and approval ------------------------------------------------- */

/* POST /api/door/unlock: same as the console "unlock" (door_control's
   timer relocks it). */
static esp_err_t handle_unlock(httpd_req_t *request) {
  if (is_refused(request)) {
    return ESP_OK;
  }
  ESP_LOGI(TAG, "Unlock requested from the app");
  const esp_err_t err = system_logic_request_unlock();
  return err == ESP_OK ? send_accepted(request)
                       : send_request_error(request, err);
}

/* POST /api/approval {"request": <id>, "approve": true|false} */
static esp_err_t handle_approval(httpd_req_t *request) {
  if (is_refused(request)) {
    return ESP_OK;
  }
  cJSON *body = read_json_body(request);
  double request_id = 0;
  const cJSON *approve = cJSON_GetObjectItemCaseSensitive(body, "approve");
  esp_err_t err = ESP_ERR_INVALID_ARG;
  if (read_whole_number(body, "request", 1, UINT32_MAX, &request_id) &&
      cJSON_IsBool(approve)) {
    err = cJSON_IsTrue(approve)
              ? system_logic_request_approve((uint32_t)request_id)
              : system_logic_request_deny((uint32_t)request_id);
  }
  cJSON_Delete(body);
  return err == ESP_OK ? send_accepted(request)
                       : send_request_error(request, err);
}

/* ---- Unlock rules ------------------------------------------------------ */

static cJSON *rules_to_json(const unlock_rules_t *rules) {
  cJSON *body = cJSON_CreateObject();
  cJSON_AddNumberToObject(body, "min_similarity_percent",
                          rules->min_similarity_percent);
  cJSON_AddNumberToObject(body, "confirm_frames", rules->confirm_frames);
  cJSON_AddNumberToObject(body, "unlock_hold_ms", rules->unlock_hold_ms);
  cJSON_AddNumberToObject(body, "cooldown_ms", rules->cooldown_ms);
  cJSON_AddNumberToObject(body, "enroll_timeout_ms", rules->enroll_timeout_ms);
  cJSON_AddNumberToObject(body, "approval_timeout_ms",
                          rules->approval_timeout_ms);
  return body;
}

static esp_err_t handle_rules_get(httpd_req_t *request) {
  if (is_refused(request)) {
    return ESP_OK;
  }
  unlock_rules_t rules;
  const esp_err_t err = system_logic_get_rules(&rules);
  return err == ESP_OK ? send_json(request, "200 OK", rules_to_json(&rules))
                       : send_request_error(request, err);
}

/* PUT /api/rules: only the keys sent change; system_logic checks ranges. */
static esp_err_t handle_rules_put(httpd_req_t *request) {
  if (is_refused(request)) {
    return ESP_OK;
  }
  unlock_rules_t rules;
  esp_err_t err = system_logic_get_rules(&rules);
  if (err != ESP_OK) {
    return send_request_error(request, err);
  }
  cJSON *body = read_json_body(request);
  if (body == NULL) {
    return send_error(request, "400 Bad Request", "JSON body expected");
  }
  double value = 0;
  bool is_ok = true;
#define READ_RULE(key, field, max)                                             \
  if (cJSON_HasObjectItem(body, key)) {                                        \
    is_ok = is_ok && read_whole_number(body, key, 0, max, &value);             \
    rules.field = is_ok ? (__typeof__(rules.field))value : rules.field;        \
  }
  READ_RULE("min_similarity_percent", min_similarity_percent, UINT8_MAX)
  READ_RULE("confirm_frames", confirm_frames, UINT8_MAX)
  READ_RULE("unlock_hold_ms", unlock_hold_ms, UINT32_MAX)
  READ_RULE("cooldown_ms", cooldown_ms, UINT32_MAX)
  READ_RULE("enroll_timeout_ms", enroll_timeout_ms, UINT32_MAX)
  READ_RULE("approval_timeout_ms", approval_timeout_ms, UINT32_MAX)
#undef READ_RULE
  cJSON_Delete(body);
  err = is_ok ? system_logic_set_rules(&rules) : ESP_ERR_INVALID_ARG;
  return err == ESP_OK ? send_json(request, "200 OK", rules_to_json(&rules))
                       : send_request_error(request, err);
}

/* ---- Camera picture settings ------------------------------------------- */

static cJSON *camera_to_json(const camera_settings_t *settings) {
  cJSON *body = cJSON_CreateObject();
  cJSON_AddNumberToObject(body, "brightness", settings->brightness);
  cJSON_AddNumberToObject(body, "contrast", settings->contrast);
  cJSON_AddNumberToObject(body, "saturation", settings->saturation);
  cJSON_AddNumberToObject(body, "sharpness", settings->sharpness);
  cJSON_AddNumberToObject(body, "exposure", settings->exposure);
  cJSON_AddNumberToObject(body, "denoise", settings->denoise);
  cJSON_AddBoolToObject(body, "vflip", settings->vflip);
  cJSON_AddBoolToObject(body, "hmirror", settings->hmirror);
  cJSON_AddNumberToObject(body, "jpeg_quality", settings->jpeg_quality);
  return body;
}

static esp_err_t handle_camera_get(httpd_req_t *request) {
  if (is_refused(request)) {
    return ESP_OK;
  }
  camera_settings_t settings;
  const esp_err_t err = camera_driver_get_settings(&settings);
  return err == ESP_OK ? send_json(request, "200 OK", camera_to_json(&settings))
                       : send_request_error(request, err);
}

/* PUT /api/camera: only the keys sent change; applied to the sensor now. */
static esp_err_t handle_camera_put(httpd_req_t *request) {
  if (is_refused(request)) {
    return ESP_OK;
  }
  camera_settings_t settings;
  esp_err_t err = camera_driver_get_settings(&settings);
  if (err != ESP_OK) {
    return send_request_error(request, err);
  }
  cJSON *body = read_json_body(request);
  if (body == NULL) {
    return send_error(request, "400 Bad Request", "JSON body expected");
  }
  double value = 0;
  bool is_ok = true;
#define READ_LEVEL(key, field)                                                 \
  if (cJSON_HasObjectItem(body, key)) {                                        \
    is_ok = is_ok && read_whole_number(body, key, INT8_MIN, INT8_MAX, &value); \
    settings.field =                                                           \
        is_ok ? (__typeof__(settings.field))value : settings.field;            \
  }
#define READ_SWITCH(key, field)                                                \
  if (cJSON_HasObjectItem(body, key)) {                                        \
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(body, key);           \
    is_ok = is_ok && cJSON_IsBool(item);                                       \
    settings.field = is_ok ? cJSON_IsTrue(item) : settings.field;              \
  }
  READ_LEVEL("brightness", brightness)
  READ_LEVEL("contrast", contrast)
  READ_LEVEL("saturation", saturation)
  READ_LEVEL("sharpness", sharpness)
  READ_LEVEL("exposure", exposure)
  READ_LEVEL("denoise", denoise)
  READ_LEVEL("jpeg_quality", jpeg_quality)
  READ_SWITCH("vflip", vflip)
  READ_SWITCH("hmirror", hmirror)
#undef READ_LEVEL
#undef READ_SWITCH
  cJSON_Delete(body);
  err = is_ok ? camera_driver_set_settings(&settings) : ESP_ERR_INVALID_ARG;
  return err == ESP_OK ? send_json(request, "200 OK", camera_to_json(&settings))
                       : send_request_error(request, err);
}

/* ---- Start ------------------------------------------------------------- */

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

  event_log_init(&s_event_log);
  s_event_log_mutex = xSemaphoreCreateMutex();
  ESP_RETURN_ON_FALSE(s_event_log_mutex != NULL, ESP_ERR_NO_MEM, TAG,
                      "no mutex");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(APP_EVENTS, ESP_EVENT_ANY_ID,
                                                 on_app_event, NULL),
                      TAG, "event register");

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = BOARD_API_PORT;
  config.ctrl_port = CONTROL_PORT;
  config.core_id = 0; /* recognition owns core 1 */
  config.max_open_sockets = MAX_OPEN_SOCKETS;
  config.max_uri_handlers = MAX_ROUTES;
  config.stack_size = STACK_SIZE_BYTES;
  config.lru_purge_enable = true;
  config.uri_match_fn = httpd_uri_match_wildcard; /* DELETE /api/faces/<id> */

  httpd_handle_t server = NULL;
  ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "http server");
  const httpd_uri_t routes[] = {
      {.uri = "/api/status", .method = HTTP_GET, .handler = handle_status},
      {.uri = "/api/events", .method = HTTP_GET, .handler = handle_events},
      {.uri = "/api/faces", .method = HTTP_GET, .handler = handle_faces_list},
      {.uri = "/api/faces/enroll",
       .method = HTTP_POST,
       .handler = handle_enroll},
      {.uri = "/api/faces/enroll/cancel",
       .method = HTTP_POST,
       .handler = handle_enroll_cancel},
      {.uri = FACES_PATH_PREFIX "*",
       .method = HTTP_DELETE,
       .handler = handle_face_delete},
      {.uri = "/api/door/unlock",
       .method = HTTP_POST,
       .handler = handle_unlock},
      {.uri = "/api/approval", .method = HTTP_POST, .handler = handle_approval},
      {.uri = "/api/rules", .method = HTTP_GET, .handler = handle_rules_get},
      {.uri = "/api/rules", .method = HTTP_PUT, .handler = handle_rules_put},
      {.uri = "/api/camera", .method = HTTP_GET, .handler = handle_camera_get},
      {.uri = "/api/camera", .method = HTTP_PUT, .handler = handle_camera_put},
  };
  for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &routes[i]), TAG,
                        "route %s", routes[i].uri);
  }
  ESP_LOGI(TAG, "App API on port %d", config.server_port);
  return ESP_OK;
}
