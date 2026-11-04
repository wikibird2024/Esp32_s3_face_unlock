idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES ai_face_recognition app_events camera_driver
                  console_commands display door_control esp_app_format
                  esp_event face_db feedback keypad_input net_comm nvs_flash
                  system_logic video_stream
)
