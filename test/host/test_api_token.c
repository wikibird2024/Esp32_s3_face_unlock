#include "api_token.h"
#include "unity.h"

#include <string.h>

void setUp(void) {}

void tearDown(void) {}

static void test_format_maps_bytes_to_the_alphabet(void) {
  const uint8_t bytes[API_TOKEN_LEN] = {0, 1, 31, 32, 255, 8, 14, 24};
  char token[API_TOKEN_LEN + 1];
  api_token_format(bytes, token);
  /* 0->A, 1->B, 31->9, 32 wraps to A, 255 % 32 = 31 -> 9, 8->J (I is
     skipped), 14->Q (O is skipped), 24->2. */
  TEST_ASSERT_EQUAL_STRING("AB9A9JQ2", token);
}

static void test_format_never_uses_look_alike_characters(void) {
  char token[API_TOKEN_LEN + 1];
  for (int start = 0; start < 256; start += API_TOKEN_LEN) {
    uint8_t bytes[API_TOKEN_LEN];
    for (int i = 0; i < API_TOKEN_LEN; i++) {
      bytes[i] = (uint8_t)(start + i);
    }
    api_token_format(bytes, token);
    TEST_ASSERT_EQUAL_size_t(API_TOKEN_LEN, strlen(token));
    TEST_ASSERT_NULL(strpbrk(token, "0O1I"));
  }
}

static void test_matches_only_the_same_full_token(void) {
  TEST_ASSERT_TRUE(api_token_matches("AB9A9JQ2", "AB9A9JQ2"));
  TEST_ASSERT_FALSE(api_token_matches("AB9A9JQ2", "AB9A9JQ3"));
  TEST_ASSERT_FALSE(api_token_matches("AB9A9JQ2", "ab9a9jq2"));
  TEST_ASSERT_FALSE(api_token_matches("AB9A9JQ2", "AB9A9JQ"));
  TEST_ASSERT_FALSE(api_token_matches("AB9A9JQ2", "AB9A9JQ2X"));
}

static void test_empty_or_null_never_matches(void) {
  TEST_ASSERT_FALSE(api_token_matches("", ""));
  TEST_ASSERT_FALSE(api_token_matches(NULL, "AB9A9JQ2"));
  TEST_ASSERT_FALSE(api_token_matches("AB9A9JQ2", NULL));
}

static void test_header_gives_the_bearer_token(void) {
  char token[API_TOKEN_LEN + 1];
  TEST_ASSERT_TRUE(api_token_from_header("Bearer AB9A9JQ2", token));
  TEST_ASSERT_EQUAL_STRING("AB9A9JQ2", token);
  TEST_ASSERT_TRUE(api_token_from_header("bearer AB9A9JQ2", token));
}

static void test_header_rejects_other_shapes(void) {
  char token[API_TOKEN_LEN + 1];
  TEST_ASSERT_FALSE(api_token_from_header(NULL, token));
  TEST_ASSERT_FALSE(api_token_from_header("", token));
  TEST_ASSERT_FALSE(api_token_from_header("Bear", token));
  TEST_ASSERT_FALSE(api_token_from_header("Basic AB9A9JQ2", token));
  TEST_ASSERT_FALSE(api_token_from_header("Bearer AB9A9JQ", token));
  TEST_ASSERT_FALSE(api_token_from_header("Bearer AB9A9JQ2X", token));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_format_maps_bytes_to_the_alphabet);
  RUN_TEST(test_format_never_uses_look_alike_characters);
  RUN_TEST(test_matches_only_the_same_full_token);
  RUN_TEST(test_empty_or_null_never_matches);
  RUN_TEST(test_header_gives_the_bearer_token);
  RUN_TEST(test_header_rejects_other_shapes);
  return UNITY_END();
}
