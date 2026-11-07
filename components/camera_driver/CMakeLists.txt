idf_component_register(
    SRCS "camera_driver.c" "camera_settings.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp32-camera settings_store
)
