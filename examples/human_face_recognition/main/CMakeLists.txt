set(src_dirs        ./)

set(include_dirs    ./)

set(requires who_spiflash_fatfs
             who_recognition_app
             esp_wifi
             esp_wifi_remote
             nvs_flash
             esp_http_server
             esp_http_client
             esp_lvgl_port
             esp_netif
             esp-brookesia)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})




