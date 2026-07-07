/**
 * @file xiaozhi_audio_bridge.cpp
 * @brief P4-side audio bridge implementation.
 */

#include "xiaozhi_audio_bridge.hpp"
#include "uart_bridge.hpp"
#include "xiaozhi/uart_frame_protocol.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char *TAG = "xz_audio";

/* ── Extern handles from app_main.cpp ─────── */
extern esp_codec_dev_handle_t g_speaker_handle;
extern esp_codec_dev_handle_t g_mic_handle;

static TaskHandle_t s_mic_task = nullptr;
static volatile bool s_running = false;
static uint16_t s_seq = 0;

/* ── PCM_DOWN frame callback ──────────────── */
static void on_pcm_down_frame(uint8_t type, const uint8_t *data, size_t len)
{
    if (type != UART_FRAME_PCM_DOWN) return;

    pcm_frame_header_t hdr;
    const int16_t *pcm;
    uint16_t count;
    if (!uart_frame_parse_pcm(data, len, &hdr, &pcm, &count))
        return;

    if (g_speaker_handle && count > 0) {
        esp_codec_dev_write(g_speaker_handle, (void *)pcm, count * sizeof(int16_t));
    }
}

/* ── Mic capture task ──────────────────────── */
static void mic_capture_task(void *arg)
{
    const int kSamplesPerFrame = 960;  // 60ms @ 16kHz
    int16_t buf[kSamplesPerFrame];
    static uint8_t fbuf[PCM_FRAME_MAX_TOTAL];  // BSS, not stack (~1928 bytes saved)

    ESP_LOGI(TAG, "Mic capture started: %d samples/frame", kSamplesPerFrame);

    while (s_running && g_mic_handle) {
        int ret = esp_codec_dev_read(g_mic_handle, buf, sizeof(buf));
        if (ret != 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        size_t flen = uart_frame_build_pcm(
            fbuf, sizeof(fbuf),
            UART_FRAME_PCM_UP, 0, s_seq++,
            buf, kSamplesPerFrame);

        if (flen > 0) {
            uart_bridge_send_frame(UART_FRAME_PCM_UP, fbuf, flen);
        }
    }

    ESP_LOGI(TAG, "Mic capture stopped");
    vTaskDelete(NULL);
}

/* ── Public API ───────────────────────────── */

void xiaozhi_audio_bridge_start(void)
{
    if (s_running) return;

    s_running = true;
    s_seq = 0;

    // Register PCM_DOWN handler
    uart_bridge_on_frame(on_pcm_down_frame);

    // Create mic capture task — use PSRAM for stack (internal DRAM is tight)
    BaseType_t ret = xTaskCreatePinnedToCore(mic_capture_task, "xz_mic",
        6144, NULL, 3, &s_mic_task, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create xz_mic task! Internal DRAM free: %lu",
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        s_running = false;
        return;
    }

    ESP_LOGI(TAG, "Xiaozhi audio bridge started (stack=4096, ret=%d)", (int)ret);
}

void xiaozhi_audio_bridge_stop(void)
{
    if (!s_running) return;

    s_running = false;

    // Wait for mic task to actually exit — it may be blocked in i2s_channel_read
    // for up to 10 seconds. We must ensure it's gone before voice_cmd resumes I2S reads.
    if (s_mic_task) {
        int wait_ms = 0;
        while (eTaskGetState(s_mic_task) != eDeleted && wait_ms < 5000) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_ms += 100;
        }
        if (wait_ms >= 5000) {
            ESP_LOGW(TAG, "Mic task did not exit after 5s, force deleting");
            vTaskDelete(s_mic_task);
        }
        s_mic_task = nullptr;
    }

    ESP_LOGI(TAG, "Xiaozhi audio bridge stopped");
}

bool xiaozhi_audio_bridge_is_active(void)
{
    return s_running;
}
