idf_component_register(
    SRCS "keypad_input.c" "key_debounce.c" "pin_entry.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES app_events esp_driver_gpio esp_event esp_rom esp_timer
                  pin_store system_logic
)
