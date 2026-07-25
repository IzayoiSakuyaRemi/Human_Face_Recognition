# ── Existing C sources ──
set(SRCS
    "uart_async_rxtxtasks_main.c"
    "csi_adr018.c"
    "event_reporter.c"
    "xiaozhi/uart_frame_protocol.c"
    "xiaozhi/xiaozhi_relay.cc"
    "xiaozhi/voice_verify.c"
)

# ── S3 shim (C++) ──
list(APPEND SRCS "xiaozhi/board.cc")

# ── xiaozhi protocol layer ──
list(APPEND SRCS
    "xiaozhi/protocols/protocol.cc"
    "xiaozhi/protocols/mqtt_protocol.cc"
    "xiaozhi/protocols/websocket_protocol.cc"
)

# ── xiaozhi core (mcp_server.cc removed — needs LVGL, not relevant for S3) ──
list(APPEND SRCS
    "xiaozhi/device_state_machine.cc"
    "xiaozhi/settings.cc"
    "xiaozhi/system_info.cc"
    "xiaozhi/ota.cc"
)

# ── xiaozhi audio ──
list(APPEND SRCS
    "xiaozhi/audio/audio_codec.cc"
    "xiaozhi/audio/audio_service.cc"
    "xiaozhi/audio/demuxer/ogg_demuxer.cc"
    "xiaozhi/audio/processors/no_audio_processor.cc"
    "xiaozhi/audio/codecs/uart_audio_codec.cc"
)

idf_component_register(
    SRCS ${SRCS}
    REQUIRES
        nvs_flash esp_wifi esp_netif esp_event lwip
        esp_driver_uart esp_driver_gpio esp_driver_i2s
        console esp_http_server esp_http_client
        esp-radar mbedtls esp_partition esp_timer
        esp_audio_codec esp_audio_effects json
        spi_flash esp_app_format app_update pthread
        esp_pm efuse mqtt esp_psram
    INCLUDE_DIRS
        "."
        "xiaozhi"
        "xiaozhi/protocols"
        "xiaozhi/audio"
        "xiaozhi/audio/processors"
        "xiaozhi/audio/demuxer"
        "xiaozhi/audio/wake_words"
        "xiaozhi/audio/codecs"
        "xiaozhi/assets"
        "xiaozhi/led"
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format" "-Wno-error=format-truncation" "-Wno-error=missing-field-initializers" "-Wno-error=unused-variable")
