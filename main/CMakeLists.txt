set(SOURCES "audio_codecs/audio_codec.cc"
            "audio_codecs/no_audio_codec.cc"
            "audio_codecs/box_audio_codec.cc"
            "audio_codecs/es8311_audio_codec.cc"
            "audio_codecs/es8374_audio_codec.cc"
            "audio_codecs/es8388_audio_codec.cc"
            "audio_processing/audio_debugger.cc"
            "led/single_led.cc"
            "led/circular_strip.cc"
            "led/gpio_led.cc"
            "display/display.cc"
            "display/lcd_display.cc"
            "display/oled_display.cc"
            "protocols/protocol.cc"
            "protocols/mqtt_protocol.cc"
            "protocols/websocket_protocol.cc"
            "iot/thing.cc"
            "iot/thing_manager.cc"
            "mcp_server.cc"
            "system_info.cc"
            "application.cc"
            "ota.cc"
            "settings.cc"
            "background_task.cc"
            "animation/animation_updater.cc"
            "sd_card.cc"
            "sd_card_startup.cc"
            "error_log_uploader.cc"
            "main.cc"
            )

set(INCLUDE_DIRS "." "display" "audio_codecs" "protocols" "audio_processing")

# Add IoT thing sources.
file(GLOB IOT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/iot/things/*.cc)
list(APPEND SOURCES ${IOT_SOURCES})

# Add shared board support sources.
file(GLOB BOARD_COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/boards/common/*.cc)
file(GLOB BOARD_COMMON_C_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/boards/common/*.c)
list(APPEND SOURCES ${BOARD_COMMON_SOURCES} ${BOARD_COMMON_C_SOURCES})
list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards/common)

# Add animation sources.
file(GLOB ANIMATION_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/animation/*.cc)
list(APPEND SOURCES ${ANIMATION_SOURCES})
file(GLOB ANIMATION_IMG_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/animation/img/*.c)
# Exclude deprecated image_50x50.c if present.
list(FILTER ANIMATION_IMG_SOURCES EXCLUDE REGEX ".*image_50x50\\.c$")
list(APPEND SOURCES ${ANIMATION_IMG_SOURCES})
list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/animation)

# animation_updater.cc is explicitly added to SOURCES list above

# EchoEar is the only supported board in this repo.
set(BOARD_TYPE "echoear")
file(GLOB BOARD_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.c
)
list(APPEND SOURCES ${BOARD_SOURCES})
# Add selected board's directory to include path so config.h resolves correctly
list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE})

if(CONFIG_USE_AUDIO_PROCESSOR)
    list(APPEND SOURCES "audio_processing/afe_audio_processor.cc")
else()
    list(APPEND SOURCES "audio_processing/no_audio_processor.cc")
endif()
if(CONFIG_USE_AFE_WAKE_WORD)
    list(APPEND SOURCES "audio_processing/afe_wake_word.cc")
elseif(CONFIG_USE_ESP_WAKE_WORD)
    list(APPEND SOURCES "audio_processing/esp_wake_word.cc")
else()
    list(APPEND SOURCES "audio_processing/no_wake_word.cc")
endif()

# Select language asset directory from Kconfig.
if(CONFIG_LANGUAGE_ZH_CN)
    set(LANG_DIR "zh-CN")
elseif(CONFIG_LANGUAGE_ZH_TW)
    set(LANG_DIR "zh-TW")
elseif(CONFIG_LANGUAGE_EN_US)
    set(LANG_DIR "en-US")
elseif(CONFIG_LANGUAGE_JA_JP)
    set(LANG_DIR "ja-JP")
endif()

# Define generated language header paths.
set(LANG_JSON "${CMAKE_CURRENT_SOURCE_DIR}/assets/${LANG_DIR}/language.json")
set(LANG_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/assets/lang_config.h")
file(GLOB LANG_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/${LANG_DIR}/*.p3)
file(GLOB COMMON_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/common/*.p3)

# If the target chip is ESP32, exclude unsupported sources.
if(CONFIG_IDF_TARGET_ESP32)
    list(REMOVE_ITEM SOURCES "audio_codecs/box_audio_codec.cc"
                             "audio_codecs/es8388_audio_codec.cc"
                             "led/gpio_led.cc"
                             )
endif()

idf_component_register(SRCS ${SOURCES}
                    EMBED_FILES ${LANG_SOUNDS} ${COMMON_SOUNDS}
                    INCLUDE_DIRS ${INCLUDE_DIRS}
                    REQUIRES bt pthread 78__esp-opus-encoder
                    PRIV_REQUIRES spi_flash esp_partition app_update esp_app_format efuse esp_hw_support esp_system esp_timer esp_event esp_pm fatfs heap nvs_flash driver cJSON console
                    WHOLE_ARCHIVE
                    )

# Define BOARD_TYPE and BOARD_NAME for firmware metadata.
# If BOARD_NAME is empty, use BOARD_TYPE.
if(NOT BOARD_NAME)
    set(BOARD_NAME ${BOARD_TYPE})
endif()
target_compile_definitions(${COMPONENT_LIB}
                    PRIVATE BOARD_TYPE=\"${BOARD_TYPE}\" BOARD_NAME=\"${BOARD_NAME}\"
                    )

# Allow passing default MQTT settings at build time, e.g.:
# idf.py -DDEFAULT_MQTT_ENDPOINT=\"192.168.1.10:1883\" -DDEFAULT_MQTT_PUBLISH_TEMPLATE=\"xiaozhi/%s/up\"
if(DEFINED DEFAULT_MQTT_ENDPOINT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DEFAULT_MQTT_ENDPOINT=\"${DEFAULT_MQTT_ENDPOINT}\")
endif()
if(DEFINED DEFAULT_MQTT_PUBLISH_TEMPLATE)
    # Note: keep %s placeholder for MAC in runtime formatting
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DEFAULT_MQTT_PUBLISH_TEMPLATE=\"${DEFAULT_MQTT_PUBLISH_TEMPLATE}\")
endif()

# Add generated language header rule.
add_custom_command(
    OUTPUT ${LANG_HEADER}
    COMMAND python ${PROJECT_DIR}/scripts/gen_lang.py
            --input "${LANG_JSON}"
            --output "${LANG_HEADER}"
    DEPENDS
        ${LANG_JSON}
        ${PROJECT_DIR}/scripts/gen_lang.py
    COMMENT "Generating ${LANG_DIR} language config"
)

# Ensure the generated language header exists before build.
add_custom_target(lang_header ALL
    DEPENDS ${LANG_HEADER}
)

# Add SPIFFS configuration for animations partition
set(ANIMATIONS_SPIFFS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/spiffs_data")
if(EXISTS ${ANIMATIONS_SPIFFS_DIR})
    spiffs_create_partition_image(
        animations
        ${ANIMATIONS_SPIFFS_DIR}
        FLASH_IN_PROJECT
    )
    message(STATUS "SPIFFS animations partition configured with data from ${ANIMATIONS_SPIFFS_DIR}")
else()
    message(WARNING "SPIFFS animations directory ${ANIMATIONS_SPIFFS_DIR} not found")
endif()
