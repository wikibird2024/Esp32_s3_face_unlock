idf_component_register(
    SRCS "api_token.c" "api_token_store.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server
    PRIV_REQUIRES esp_hw_support nvs_flash
)
