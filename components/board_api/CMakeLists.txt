idf_component_register(
    SRCS "board_api.c" "event_log.c"
    INCLUDE_DIRS "."
    REQUIRES face_db
    PRIV_REQUIRES api_token app_events camera_driver door_control
                  esp_app_format esp_event esp_http_server esp_timer json
                  net_comm system_logic
)
