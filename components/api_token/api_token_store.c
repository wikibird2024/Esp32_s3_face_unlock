#include "api_token_store.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

static const char *TAG = "api_token";

#define NVS_NAMESPACE "api"
#define NVS_KEY "token"
/* "Bearer " + token + spare for a malformed, longer value. */
#define HEADER_VALUE_MAX_LEN 32
#define QUERY_MAX_LEN 64

/* Written by api_token_store_init/renew, read by the HTTP server tasks. */
static char s_token[API_TOKEN_LEN + 1];
static portMUX_TYPE s_token_lock = portMUX_INITIALIZER_UNLOCKED;

static void set_token(const char token[API_TOKEN_LEN + 1]) {
  portENTER_CRITICAL(&s_token_lock);
  memcpy(s_token, token, API_TOKEN_LEN + 1);
  portEXIT_CRITICAL(&s_token_lock);
}

static esp_err_t save_new_token(char out_token[API_TOKEN_LEN + 1]) {
  uint8_t random_bytes[API_TOKEN_LEN];
  esp_fill_random(random_bytes, sizeof(random_bytes));
  api_token_format(random_bytes, out_token);

  nvs_handle_t handle;
  ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG,
                      "nvs open");
  esp_err_t err = nvs_set_str(handle, NVS_KEY, out_token);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}

esp_err_t api_token_store_init(void) {
  char token[API_TOKEN_LEN + 1] = "";
  api_token_store_get(token);
  if (token[0] != '\0') {
    return ESP_OK;
  }

  nvs_handle_t handle;
  size_t length_bytes = sizeof(token);
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_OK) {
    err = nvs_get_str(handle, NVS_KEY, token, &length_bytes);
    nvs_close(handle);
  }
  /* A missing or damaged token is replaced; the app must then be told
     the new one, which the boot log shows. */
  if (err != ESP_OK || strlen(token) != API_TOKEN_LEN) {
    ESP_LOGI(TAG, "No token yet, making one");
    ESP_RETURN_ON_ERROR(save_new_token(token), TAG, "save token");
  }
  set_token(token);
  return ESP_OK;
}

void api_token_store_get(char out_token[API_TOKEN_LEN + 1]) {
  portENTER_CRITICAL(&s_token_lock);
  memcpy(out_token, s_token, API_TOKEN_LEN + 1);
  portEXIT_CRITICAL(&s_token_lock);
}

esp_err_t api_token_store_renew(void) {
  char token[API_TOKEN_LEN + 1];
  ESP_RETURN_ON_ERROR(save_new_token(token), TAG, "save token");
  set_token(token);
  return ESP_OK;
}

/* Copies the token the request carries into out_token; false if none. */
static bool token_from_request(httpd_req_t *request,
                               char out_token[API_TOKEN_LEN + 1]) {
  char header[HEADER_VALUE_MAX_LEN];
  if (httpd_req_get_hdr_value_str(request, "Authorization", header,
                                  sizeof(header)) == ESP_OK &&
      api_token_from_header(header, out_token)) {
    return true;
  }
  char query[QUERY_MAX_LEN];
  return httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK &&
         httpd_query_key_value(query, "token", out_token, API_TOKEN_LEN + 1) ==
             ESP_OK;
}

bool api_token_store_request_is_allowed(httpd_req_t *request) {
  char expected[API_TOKEN_LEN + 1];
  char given[API_TOKEN_LEN + 1];
  api_token_store_get(expected);
  return token_from_request(request, given) &&
         api_token_matches(expected, given);
}

esp_err_t api_token_store_send_unauthorized(httpd_req_t *request) {
  httpd_resp_set_status(request, "401 Unauthorized");
  httpd_resp_set_type(request, "application/json");
  return httpd_resp_sendstr(request, "{\"error\":\"missing or wrong token\"}");
}
