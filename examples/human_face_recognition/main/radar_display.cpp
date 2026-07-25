#include "radar_display.hpp"
#include "who_recognition_app_lcd.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "lvgl.h"
#include "driver/gpio.h"
#include "esp_brookesia.hpp"

// GPIO 20 for backlight (BSP defines it as 26 for upstream board; this board uses 20)
#define BK_GPIO GPIO_NUM_20

extern ESP_Brookesia_Phone *g_phone;
#include <cstdio>
#include <cstring>

static const char *TAG = "radar_disp";

/* Queue for radar data from uart_rx_task → display task */
typedef struct { char room[12], move[12]; float wander, jitter; } radar_msg_t;
static QueueHandle_t s_radar_queue = NULL;

/* ── Screen sleep/wake state ───────────────── */
static bool s_screen_on = true;
static TickType_t s_last_moving = 0;
static const int IDLE_TIMEOUT_S = 10;

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

/* FreeRTOS task — polls queue, updates LVGL, manages screen sleep */
static void radar_display_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    s_last_moving = xTaskGetTickCount();

    // Take over GPIO 20 for direct backlight control
    gpio_config_t bk_cfg = { .pin_bit_mask = BIT64(BK_GPIO),
                             .mode = GPIO_MODE_OUTPUT,
                             .pull_up_en = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type = GPIO_INTR_DISABLE };
    gpio_config(&bk_cfg);
    gpio_set_level(BK_GPIO, 1);

    ESP_LOGI(TAG, "Radar display task started (screen timeout=%ds)", IDLE_TIMEOUT_S);

    extern who::app::WhoRecognitionAppLCD *g_recognition_app;
    radar_msg_t msg;

    while (1) {
        if (xQueueReceive(s_radar_queue, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ESP_LOGD(TAG, "S3 radar: room=%s move=%s w=%.4f j=%.4f",
                     msg.room, msg.move, msg.wander, msg.jitter);

            if (g_recognition_app) {
                char buf[128];
                snprintf(buf, sizeof(buf), "S3: %s %s  j=%.3f", msg.room, msg.move, msg.jitter);
                g_recognition_app->set_status_text(buf);
            }

            /* ── Screen sleep/wake based on move ── */
            if (strstr(msg.move, "MOVING")) {
                s_last_moving = xTaskGetTickCount();
                if (!s_screen_on) {
                    gpio_set_level(BK_GPIO, 1);
                    s_screen_on = true;
                    ESP_LOGI(TAG, "Screen ON (movement detected)");
                    // Auto-launch Camera app on wake
                    if (g_phone) g_phone->getCoreManager().startApp(0);
                }
            }
        }

        /* Check idle timeout (runs every 1s via queue timeout) */
        if (s_screen_on) {
            // Keep screen on when any app is active (Camera, XiaoZhi, Settings)
            if (g_phone && g_phone->getCoreManager().getActiveApp()) {
                s_last_moving = xTaskGetTickCount();
            } else {
                uint32_t touch_idle = lv_display_get_inactive_time(NULL);
                if (touch_idle < 2000) s_last_moving = xTaskGetTickCount();

                TickType_t elapsed = xTaskGetTickCount() - s_last_moving;
                if (elapsed > pdMS_TO_TICKS(IDLE_TIMEOUT_S * 1000)) {
                    gpio_set_level(BK_GPIO, 0);
                    s_screen_on = false;
                    ESP_LOGI(TAG, "Screen OFF (idle %lus)", (unsigned long)(elapsed * portTICK_PERIOD_MS / 1000));
                }
            }
        } else {
            // Wake on touch even without radar movement
            uint32_t touch_idle = lv_display_get_inactive_time(NULL);
            if (touch_idle < 2000) {
                gpio_set_level(BK_GPIO, 1);
                s_screen_on = true;
                s_last_moving = xTaskGetTickCount();
                ESP_LOGI(TAG, "Screen ON (touch wake)");
                if (g_phone) g_phone->getCoreManager().startApp(0);
            }
        }
    }
}

bool radar_display_is_screen_on(void)
{
    return s_screen_on;
}

void radar_display_wake_screen(void)
{
    if (!s_screen_on) {
        gpio_set_level(BK_GPIO, 1);
        s_screen_on = true;
        s_last_moving = xTaskGetTickCount();
        ESP_LOGI(TAG, "Screen ON (touch wake)");
    }
}

void radar_display_keep_awake(void)
{
    s_last_moving = xTaskGetTickCount();
    if (!s_screen_on) {
        gpio_set_level(BK_GPIO, 1);
        s_screen_on = true;
    }
}

void radar_display_init(void)
{
    s_radar_queue = xQueueCreate(4, sizeof(radar_msg_t));
    xTaskCreate(radar_display_task, "radar_disp", 2560, NULL, 2, NULL);
    ESP_LOGI(TAG, "Radar display queue ready (task starts after LVGL init)");
}
