idf_component_register(
    SRCS "board_api.c"
    INCLUDE_DIRS "."
    REQUIRES face_db
    PRIV_REQUIRES api_token door_control esp_app_format esp_http_server json
                  net_comm system_logic
)
