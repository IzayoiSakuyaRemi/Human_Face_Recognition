#include "event_reporter.hpp"
#include "uart_bridge.hpp"
#include <cstring>
#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_http_client.h>
#include <esp_log.h>

static const char *TAG = "reporter";
static const char *SERVER_URL = CONFIG_EVENT_SERVER_URL;
static QueueHandle_t s_report_queue = nullptr;

struct ReportTask {
    char body[256];
};

static void reporter_task(void *arg) {
    ReportTask task;
    while (xQueueReceive(s_report_queue, &task, portMAX_DELAY)) {
        esp_http_client_config_t cfg = {
            .url = SERVER_URL,
            .method = HTTP_METHOD_POST,
            .timeout_ms = 3000,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, task.body, strlen(task.body));
        esp_err_t err = esp_http_client_perform(client);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "POST failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Reported: %s", task.body);
        }
        esp_http_client_cleanup(client);
    }
    vTaskDelete(NULL);
}

void report_event(const char *event, const char *json_fields) {
    if (!s_report_queue) {
        s_report_queue = xQueueCreate(16, sizeof(ReportTask));
        xTaskCreate(reporter_task, "reporter", 4096, NULL, 3, NULL);
    }
    ReportTask task;
    snprintf(task.body, sizeof(task.body), "{\"device\":\"esp32-p4\",\"event\":\"%s\",%s}", event, json_fields);
    xQueueSend(s_report_queue, &task, 0);
    uart_bridge_send(task.body);   // also forward to S3 via UART
}
