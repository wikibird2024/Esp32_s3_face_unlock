idf_component_register(
    SRCS "test_main.c" "test_face_db_nvs.c" "test_door_control.c"
         "test_pin_store.c"
    PRIV_REQUIRES unity face_db door_control esp_driver_gpio nvs_flash
                  pin_store
    WHOLE_ARCHIVE
)
