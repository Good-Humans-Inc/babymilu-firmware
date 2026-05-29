set(SOURCES
    "main.cc"
    "audio_codec.cc"
    "box_audio_codec.cc"
    "afe_audio_processor.cc"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES
        cJSON
        driver
        esp_event
        esp_netif
        esp_timer
        esp_wifi
        esp_websocket_client
        nvs_flash
        78__esp-opus-encoder
)

