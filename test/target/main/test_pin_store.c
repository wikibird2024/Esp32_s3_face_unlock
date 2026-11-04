#include "nvs_flash.h"
#include "pin_store.h"
#include "unity.h"

static void start_with_no_pin(void) {
  static bool is_initialised;
  if (!is_initialised) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_erase());
      err = nvs_flash_init();
    }
    TEST_ASSERT_EQUAL(ESP_OK, err);
    is_initialised = true;
  }
  TEST_ASSERT_EQUAL(ESP_OK, pin_store_clear());
}

TEST_CASE("no PIN opens the door until one is set", "[pin]") {
  start_with_no_pin();
  TEST_ASSERT_FALSE(pin_store_has_pin());
  TEST_ASSERT_FALSE(pin_store_check("0000"));
}

TEST_CASE("only the right PIN passes", "[pin]") {
  start_with_no_pin();
  TEST_ASSERT_EQUAL(ESP_OK, pin_store_set("123456"));
  TEST_ASSERT_TRUE(pin_store_has_pin());
  TEST_ASSERT_TRUE(pin_store_check("123456"));
  TEST_ASSERT_FALSE(pin_store_check("123457"));
  TEST_ASSERT_FALSE(pin_store_check("12345"));
  TEST_ASSERT_FALSE(pin_store_check(""));
}

TEST_CASE("a bad PIN cannot be set", "[pin]") {
  start_with_no_pin();
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pin_store_set("123"));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pin_store_set("123456789"));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pin_store_set("12a4"));
  TEST_ASSERT_FALSE(pin_store_has_pin());
}

TEST_CASE("setting a PIN resets the wrong tries", "[pin]") {
  start_with_no_pin();
  TEST_ASSERT_EQUAL(ESP_OK, pin_store_save_failed_count(7U));
  TEST_ASSERT_EQUAL_UINT32(7U, pin_store_failed_count());
  TEST_ASSERT_EQUAL(ESP_OK, pin_store_set("1234"));
  TEST_ASSERT_EQUAL_UINT32(0U, pin_store_failed_count());
}

TEST_CASE("the same PIN gets a different stored hash each time", "[pin]") {
  start_with_no_pin();
  TEST_ASSERT_EQUAL(ESP_OK, pin_store_set("1234"));
  TEST_ASSERT_TRUE(pin_store_check("1234"));
  TEST_ASSERT_EQUAL(ESP_OK, pin_store_set("1234"));
  TEST_ASSERT_TRUE(pin_store_check("1234"));
}
