/**
 * @file event_reporter.c
 * @brief HTTP POST radar events to Flask server (async, non-blocking).
 *
 * Mirrors D:\works\esp-who-master\examples\human_face_recognition\main\event_reporter.cpp
 * but written in C for the S3 project.
 */

#include "event_reporter.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>

#define REPORT_QUEUE_DEPTH 8
#define REPORT_BODY_MAX    256

static const char *TAG = "reporter";
static QueueHandle_t s_queue = NULL;

typedef struct {
    char body[REPORT_BODY_MAX];
} report_msg_t;

static void reporter_task(void *arg)
{
    report_msg_t msg;

    while (xQueueReceive(s_queue, &msg, portMAX_DELAY)) {
        esp_http_client_config_t cfg = {
            .url = "http://10.53.101.96:8765/api/event",
            .method = HTTP_METHOD_POST,
            .timeout_ms = 3000,
        };
        esp_http_client_handle_t cli = esp_http_client_init(&cfg);
        esp_http_client_set_header(cli, "Content-Type", "application/json");
        esp_err_t err = esp_http_client_open(cli, strlen(msg.body));
        if (err == ESP_OK) {
            esp_http_client_write(cli, msg.body, strlen(msg.body));
            esp_http_client_fetch_headers(cli);
        }
        if (err != ESP_OK)
            ESP_LOGW(TAG, "POST failed: %d", err);
        esp_http_client_cleanup(cli);
    }
    vTaskDelete(NULL);
}

void event_reporter_init(void)
{
    s_queue = xQueueCreate(REPORT_QUEUE_DEPTH, sizeof(report_msg_t));
    xTaskCreate(reporter_task, "reporter", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "Reporter started → http://10.221.33.96:8765/api/event");
}

void report_radar_event(bool room, bool moving, float wander, float jitter)
{
    if (!s_queue) return;

    report_msg_t msg;
    snprintf(msg.body, sizeof(msg.body),
        "{\"device\":\"s3-radar\",\"event\":\"radar_status\","
        "\"room\":\"%s\",\"move\":\"%s\",\"wander\":%.4f,\"jitter\":%.4f}",
        room ? "OCCUPIED" : "EMPTY",
        moving ? "MOVING" : "still",
        wander, jitter);

    xQueueSend(s_queue, &msg, 0);
}

/* Forward P4 voice event JSON directly to HTTP server.
 * P4 sends: {"dev":"p4-voice","event":"voice_command","cmd_id":1,...}
 * Server expects same format. Just relay as-is. */
void report_p4_event(const char *json_line)
{
    if (!s_queue) return;

    report_msg_t msg;
    strncpy(msg.body, json_line, sizeof(msg.body) - 1);
    msg.body[sizeof(msg.body) - 1] = 0;
    xQueueSend(s_queue, &msg, 0);
}
