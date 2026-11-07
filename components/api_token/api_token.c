#include "api_token.h"

#include <ctype.h>
#include <string.h>

/* 32 symbols: 256 is a multiple of 32, so byte % 32 has no bias. */
static const char ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
#define ALPHABET_SIZE (sizeof(ALPHABET) - 1)
#define BEARER_PREFIX "Bearer "
#define BEARER_PREFIX_LEN (sizeof(BEARER_PREFIX) - 1)

_Static_assert(ALPHABET_SIZE == 32, "token alphabet must have 32 symbols");

void api_token_format(const uint8_t random_bytes[API_TOKEN_LEN],
                      char out_token[API_TOKEN_LEN + 1]) {
  for (int i = 0; i < API_TOKEN_LEN; i++) {
    out_token[i] = ALPHABET[random_bytes[i] % ALPHABET_SIZE];
  }
  out_token[API_TOKEN_LEN] = '\0';
}

bool api_token_matches(const char *expected, const char *given) {
  if (expected == NULL || given == NULL ||
      strnlen(expected, API_TOKEN_LEN + 1) != API_TOKEN_LEN ||
      strnlen(given, API_TOKEN_LEN + 1) != API_TOKEN_LEN) {
    return false;
  }
  unsigned difference = 0;
  for (int i = 0; i < API_TOKEN_LEN; i++) {
    difference |= (unsigned)(expected[i] ^ given[i]);
  }
  return difference == 0;
}

bool api_token_from_header(const char *authorization,
                           char out_token[API_TOKEN_LEN + 1]) {
  if (authorization == NULL) {
    return false;
  }
  /* RFC 7235: the scheme name is case-insensitive. */
  for (size_t i = 0; i < BEARER_PREFIX_LEN; i++) {
    if (tolower((unsigned char)authorization[i]) !=
        tolower((unsigned char)BEARER_PREFIX[i])) {
      return false;
    }
  }
  const char *token = authorization + BEARER_PREFIX_LEN;
  if (strlen(token) != API_TOKEN_LEN) {
    return false;
  }
  memcpy(out_token, token, API_TOKEN_LEN + 1);
  return true;
}
