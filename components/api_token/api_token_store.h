/**
 * @file api_token_store.h
 * @brief The board's one app token: kept in the default NVS partition,
 *        checked on every HTTP request to the API and the video stream.
 *
 * The token is shown only in the boot log and by the console command
 * `token`, both of which need hands on the board. It is never shown on
 * the OLED: that screen faces the visitor.
 */
#pragma once

#include <stdbool.h>

#include "api_token.h"
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load the token, or make and save a new one on the first start. Call after
 * Wi-Fi has started: only then is esp_fill_random() truly random. Safe to
 * call again (does nothing).
 */
esp_err_t api_token_store_init(void);

/** Current token, or "" before api_token_store_init(). Any task. */
void api_token_store_get(char out_token[API_TOKEN_LEN + 1]);

/** Make, save and switch to a new token; the old one stops working. */
esp_err_t api_token_store_renew(void);

/**
 * True when the request carries the current token, as
 * "Authorization: Bearer <token>" or as "?token=<token>" (for a browser).
 * Always false before api_token_store_init().
 */
bool api_token_store_request_is_allowed(httpd_req_t *request);

/** Answer 401 with a short JSON error. */
esp_err_t api_token_store_send_unauthorized(httpd_req_t *request);

#ifdef __cplusplus
}
#endif
