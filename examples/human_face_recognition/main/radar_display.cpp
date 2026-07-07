#include "radar_display.hpp"
#include "who_recognition_app_lcd.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "radar_disp";

/* Queue for radar data from uart_rx_task → display task */
typedef struct { char room[12], move[12]; float wander, jitter; } radar_msg_t;
static QueueHandle_t s_radar_queue = NULL;

/* Called by uart_bridge to push parsed radar data */
void radar_display_push(const char *room, const char *move,
                         float wander, float jitter)
{
    if (!s_radar_queue) return;
    radar_msg_t msg;
    snprintf(msg.room, sizeof(msg.room), "%s", room);
    snprintf(msg.move, sizeof(msg.move), "%s", move);
    msg.wander = wander;
    msg.jitter = jitter;
    xQueueSend(s_radar_queue, &msg, 0);
}

/* FreeRTOS task — polls queue, updates LVGL (runs after LVGL is ready) */
static void radar_display_task(void *arg)
{
    /* Wait for brookesia/LVGL to initialize (Ethernet takes ~3s, give 5s margin) */
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "Radar display task started");

    extern who::app::WhoRecognitionAppLCD *g_recognition_app;
    radar_msg_t msg;

    while (1) {
        if (xQueueReceive(s_radar_queue, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ESP_LOGI(TAG, "S3 radar: room=%s move=%s w=%.4f j=%.4f",
                     msg.room, msg.move, msg.wander, msg.jitter);

            if (g_recognition_app) {
                char buf[128];
                snprintf(buf, sizeof(buf), "S3: %s %s  j=%.3f", msg.room, msg.move, msg.jitter);
                g_recognition_app->set_status_text(buf);
            }
        }
    }
}

void radar_display_init(void)
{
    s_radar_queue = xQueueCreate(4, sizeof(radar_msg_t));
    xTaskCreate(radar_display_task, "radar_disp", 3072, NULL, 2, NULL);
    ESP_LOGI(TAG, "Radar display queue ready (task starts after LVGL init)");
}
