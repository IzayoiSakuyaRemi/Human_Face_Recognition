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

    ESP_LOGI(TAG, "PCM_DOWN rx: seq=%u samples=%u speaker=%p",
             (unsigned)hdr.seq, (unsigned)count, (void *)g_speaker_handle);

#if 0  // DISABLED: I2S TX DMA corrupts s_frame_cbs[] function pointers
    if (g_speaker_handle && count > 0) {
        ESP_LOGI(TAG, "PCM_DOWN -> esp_codec_dev_write(%d bytes)", (int)(count * sizeof(int16_t)));
        esp_codec_dev_write(g_speaker_handle, (void *)pcm, count * sizeof(int16_t));
        ESP_LOGI(TAG, "PCM_DOWN write done");
    }
#endif
    ESP_LOGI(TAG, "PCM_DOWN speaker write SKIPPED (disabled for debug)");
}

/* ── Mic capture task ──────────────────────── */
static void mic_capture_task(void *arg)
{
    const int kSamplesPerFrame = 960;  // 60ms @ 16kHz
    // CRITICAL: I2S DMA buffer is 512 bytes. Reading >512 bytes per call
    // requires multi-DMA-transfer collection, which triggers driver bugs on
    // ESP32-P4 (v5.5.4) causing memory corruption. Read in 240-sample chunks
    // (480 bytes < 512), accumulate 4 reads per PCM_UP frame.
    const int kChunkSamples = 240;  // 240 samples × 2 bytes = 480 bytes < 512 DMA buf
    const int kChunksPerFrame = kSamplesPerFrame / kChunkSamples;  // 4 reads per frame
    int16_t buf[kSamplesPerFrame];
    static uint8_t fbuf[PCM_FRAME_MAX_TOTAL];  // BSS (1928 bytes)

    ESP_LOGI(TAG, "Mic capture started: %d samples/frame, %d samples/chunk × %d chunks, buf=%p (stack) fbuf=%p (BSS) stack_hwm=%lu",
             kSamplesPerFrame, kChunkSamples, kChunksPerFrame, (void *)buf, (void *)fbuf,
             uxTaskGetStackHighWaterMark(NULL));

    uint32_t read_ok = 0, read_fail = 0;
    TickType_t last_report = xTaskGetTickCount();

    while (s_running && g_mic_handle) {
        // Read 4 small chunks to build one 960-sample frame
        for (int chunk = 0; chunk < kChunksPerFrame; chunk++) {
            int16_t *dest = buf + chunk * kChunkSamples;
            int ret = esp_codec_dev_read(g_mic_handle, dest,
                                          sizeof(int16_t) * kChunkSamples);
            if (ret != 0) {
                read_fail++;
                vTaskDelay(pdMS_TO_TICKS(1));
                chunk--;  // retry this chunk
                continue;
            }
        }
        read_ok++;

        // Periodic stats every 10 seconds
        TickType_t now = xTaskGetTickCount();
        if (now - last_report >= pdMS_TO_TICKS(10000)) {
            ESP_LOGI(TAG, "Mic stats: ok=%lu fail=%lu hwm=%lu",
                     read_ok, read_fail,
                     uxTaskGetStackHighWaterMark(NULL));
            last_report = now;
        }

        size_t flen = uart_frame_build_pcm(
            fbuf, sizeof(fbuf),
            UART_FRAME_PCM_UP, 0, s_seq++,
            buf, kSamplesPerFrame);

        if (flen > 0) {
            uart_bridge_send_frame(UART_FRAME_PCM_UP, fbuf, flen);
        }
    }

    ESP_LOGI(TAG, "Mic capture stopped: ok=%lu fail=%lu final_hwm=%lu",
             read_ok, read_fail, uxTaskGetStackHighWaterMark(NULL));
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

    // Create mic capture task on CPU1 — separates heavy mic I2S reads from
    // CPU0's ISR load (I2S ISR + UART ISR + ESP timer). LVGL is idle during
    // xiaozhi mode (face detection paused), so CPU1 has plenty of bandwidth.
    BaseType_t ret = xTaskCreatePinnedToCore(mic_capture_task, "xz_mic",
        8192, NULL, 3, &s_mic_task, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create xz_mic task! Internal DRAM free: %lu",
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        s_running = false;
        return;
    }

    ESP_LOGI(TAG, "Xiaozhi audio bridge started (stack=8192, core=1, ret=%d)", (int)ret);
}

void xiaozhi_audio_bridge_stop(void)
{
    if (!s_running) return;

    s_running = false;

    // Wait for mic task to actually exit
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
