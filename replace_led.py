with open(r'D:\chengxu\ESP\esp-csi-master\esp-csi-master\examples\esp-radar\console_test\main\app_main.c', 'r') as f:
    content = f.read()

# 1. Replace training LED blink with printf
old_train = '    if (g_console_input_config.train_start) {\n        s_last_move_time    = esp_log_timestamp();\n        s_last_someone_time = esp_log_timestamp();\n\n        static bool led_status = false;\n\n        if (led_status) {\n            led_strip_set_pixel(led_strip, 0, 0, 0, 0);\n        } else {\n            led_strip_set_pixel(led_strip, 0, 255, 255, 0);\n        }\n        led_status = !led_status;\n        led_strip_refresh(led_strip);\n        return;\n    }'

new_train = '    if (g_console_input_config.train_start) {\n        s_last_move_time    = esp_log_timestamp();\n        s_last_someone_time = esp_log_timestamp();\n\n        printf(\"=== TRAINING in progress ===\\n\");\n        return;\n    }'

content = content.replace(old_train, new_train)

# 2. Replace LED status display with monitor output
old_status = '    if (room_status) {\n        if (human_status) {\n            led_strip_set_pixel(led_strip, 0, 0, 255, 0);\n            ESP_LOGI(TAG, \"Someone moved\");\n            s_last_move_time = esp_log_timestamp();\n        } else if (esp_log_timestamp() - s_last_move_time > 3 * 1000) {\n            led_strip_set_pixel(led_strip, 0, 255, 255, 255);\n            ESP_LOGI(TAG, \"Someone\");\n        }\n\n        s_last_someone_time = esp_log_timestamp();\n    } else if (esp_log_timestamp() - s_last_someone_time > 3 * 1000) {\n        if (human_status) {\n            s_last_move_time = esp_log_timestamp();\n            led_strip_set_pixel(led_strip, 0, 255, 0, 0);\n        } else if (esp_log_timestamp() - s_last_move_time > 3 * 1000) {\n            led_strip_set_pixel(led_strip, 0, 0, 0, 0);\n        }\n    }\n    led_strip_refresh(led_strip);'

new_status = '    if (room_status) {\n        if (human_status) {\n            ESP_LOGI(TAG, \">>> [OCCUPIED] SOMEONE MOVING <<<\");\n            s_last_move_time = esp_log_timestamp();\n        } else if (esp_log_timestamp() - s_last_move_time > 3 * 1000) {\n            ESP_LOGI(TAG, \">>> [OCCUPIED] someone present (still) <<<\");\n        }\n        s_last_someone_time = esp_log_timestamp();\n    } else if (esp_log_timestamp() - s_last_someone_time > 3 * 1000) {\n        if (human_status) {\n            s_last_move_time = esp_log_timestamp();\n            ESP_LOGI(TAG, \">>> [EMPTY] transient movement >>>\");\n        } else if (esp_log_timestamp() - s_last_move_time > 3 * 1000) {\n            ESP_LOGI(TAG, \">>> [EMPTY] no one present <<<\");\n        }\n    }'

content = content.replace(old_status, new_status)

with open(r'D:\chengxu\ESP\esp-csi-master\esp-csi-master\examples\esp-radar\console_test\main\app_main.c', 'w') as f:
    f.write(content)

print('done')
