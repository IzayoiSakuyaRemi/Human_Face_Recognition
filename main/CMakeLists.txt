idf_component_register(SRCS "uart_async_rxtxtasks_main.c"
                    REQUIRES nvs_flash esp_wifi esp_netif esp_event
                             esp_driver_uart esp_driver_gpio console esp_http_server
                    INCLUDE_DIRS ".")
