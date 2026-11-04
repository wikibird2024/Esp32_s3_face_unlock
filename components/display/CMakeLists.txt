idf_component_register(
    SRCS "display.c" "display_render.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES app_events esp_driver_i2c esp_event esp_lcd esp_netif
)
