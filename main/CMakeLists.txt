idf_component_register(SRCS "uart_async_rxtxtasks_main.c" "csi_adr018.c" "event_reporter.c"
                           "xiaozhi/uart_frame_protocol.c"
                    REQUIRES nvs_flash esp_wifi esp_netif esp_event lwip
                             esp_driver_uart esp_driver_gpio console esp_http_server
                             esp-radar mbedtls esp_partition esp_timer esp_http_client
                    INCLUDE_DIRS "." "xiaozhi")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format" "-Wno-error=format-truncation")
