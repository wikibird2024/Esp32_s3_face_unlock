idf_component_register(
    SRCS "console_commands.c"
    INCLUDE_DIRS "."
    REQUIRES face_db
    PRIV_REQUIRES console door_control keypad_input net_comm pin_store
                  system_logic
)
