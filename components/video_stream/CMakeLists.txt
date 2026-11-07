idf_component_register(
    SRCS "video_stream.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES api_token camera_driver esp_http_server net_comm
)
