idf_component_register(
    SRCS "pin_store.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_hw_support mbedtls nvs_flash
)
