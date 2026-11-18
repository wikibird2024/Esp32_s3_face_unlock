idf_component_register(
    SRCS "console_commands.c" "console_json.c"
    INCLUDE_DIRS "."
    REQUIRES face_db
    PRIV_REQUIRES console door_control esp_app_format esp_timer net_comm
                  system_logic
)
