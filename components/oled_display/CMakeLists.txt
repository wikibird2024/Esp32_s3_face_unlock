idf_component_register(
    SRCS "oled_display.c" "display_text.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES app_events esp_driver_i2c esp_event esp_lcd
)
