set(src_dirs        ./ ./xiaozhi)
set(include_dirs    ./ ./xiaozhi)
set(requires who_spiflash_fatfs
             who_recognition_app
             nvs_flash
             esp_http_server
             esp_http_client
             esp_netif
             esp_eth
             esp_driver_uart
             esp_lvgl_port
             esp-brookesia)
idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-error=format")
