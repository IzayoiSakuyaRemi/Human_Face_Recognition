idf_component_register(SRCS "uart_async_rxtxtasks_main.c"
                    REQUIRES nvs_flash esp_wifi esp_netif esp_event lwip
                             esp_driver_uart esp_driver_gpio console esp_http_server
                             esp-radar mbedtls esp_partition
                    INCLUDE_DIRS ".")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format" "-Wno-error=format-truncation")
