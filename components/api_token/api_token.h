/**
 * @file api_token.h
 * @brief Format, compare and parse the app's login token.
 *
 * Plain C with no ESP-IDF includes so it runs in host tests. Storage and
 * HTTP checks are in api_token_store.h.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 8 characters of 5 bits each: 40 bits. */
#define API_TOKEN_LEN 8

/**
 * Turn API_TOKEN_LEN random bytes into a token. The alphabet has no 0/O or
 * 1/I so the token is easy to read off a log and type on a phone.
 */
void api_token_format(const uint8_t random_bytes[API_TOKEN_LEN],
                      char out_token[API_TOKEN_LEN + 1]);

/**
 * True when `given` equals `expected` and both are API_TOKEN_LEN long.
 * The comparison time does not depend on where the first wrong character
 * is. NULL or empty never matches.
 */
bool api_token_matches(const char *expected, const char *given);

/**
 * Copy the token out of an HTTP "Authorization: Bearer <token>" value.
 * False when the value is not a Bearer token of API_TOKEN_LEN characters.
 */
bool api_token_from_header(const char *authorization,
                           char out_token[API_TOKEN_LEN + 1]);

#ifdef __cplusplus
}
#endif
