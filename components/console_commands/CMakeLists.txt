idf_component_register(
    SRCS "console_commands.c"
    INCLUDE_DIRS "."
    REQUIRES face_db
    PRIV_REQUIRES api_token console door_control net_comm system_logic
)
