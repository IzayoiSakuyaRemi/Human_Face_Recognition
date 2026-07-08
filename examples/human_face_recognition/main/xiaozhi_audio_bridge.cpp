/**
 * @file xiaozhi_audio_bridge.cpp
 * @brief P4-side audio bridge — 512-sample reads (voice_cmd proven pattern).
 *
 * voice_cmd reads 512 samples (1024 bytes = 2 DMA buffers, exact fit) and is
 * stable.  xz_mic mirrors this exact pattern: two 512-sample reads per cycle,
 * accumulated in a large PSRAM ring buffer.  960-sample PCM_UP frames are
 * drained from the ring whenever ≥960 samples are available.
 *
 * Net inflow per cycle: 2×512=1024.  Net outflow: 960.  Surplus: +64 samples.
 * Ring buffer must absorb this surplus for the full session duration.
 */

#include "xiaozhi_audio_bridge.hpp"
#include "uart_bridge.hpp"
#include "xiaozhi/uart_frame_protocol.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <cstring>

static const char *TAG = "xz_audio";

/* ── Extern handles ───────────────────────── */
extern esp_codec_dev_handle_t g_speaker_handle;
extern esp_codec_dev_handle_t g_mic_handle;

/* ── I2S mutex: serialize TX + RX on shared I2S peripheral ── */
static SemaphoreHandle_t s_i2s_mutex = nullptr;

/* ── Constants ────────────────────────────── */
#define SAMPLES_PER_READ     512   // Exactly 2 DMA buffers — voice_cmd's proven pattern
#define READ_BYTES           (SAMPLES_PER_READ * sizeof(int16_t))  // = 1024
#define SAMPLES_PER_FRAME    960   // xiaozhi protocol: 60ms @ 16kHz
#define READS_PER_CYCLE      2     // Two 512-sample reads per cycle

// Ring buffer: PSRAM, sized for ~60s session at +64 samples/cycle surplus
// (60s × 16.7 Hz × 64 samples) + 2×512 safety ≈ 64K samples = 128 KiB
#define RING_SAMPLES         65536
#define RING_MASK            (RING_SAMPLES - 1)  // power-of-2 → bitwise wrap

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

    if (g_speaker_handle && count > 0 && s_i2s_mutex) {
        xSemaphoreTake(s_i2s_mutex, portMAX_DELAY);
        esp_codec_dev_write(g_speaker_handle, (void *)pcm, count * sizeof(int16_t));
        xSemaphoreGive(s_i2s_mutex);
    }
}

/* ── Ring buffer helpers ──────────────────── */
static inline void ring_write(int16_t *ring, volatile int *wpos,
                              const int16_t *src, int count)
{
    int pos = *wpos;
    int first = (RING_SAMPLES - pos < count) ? RING_SAMPLES - pos : count;
    memcpy(&ring[pos], src, first * sizeof(int16_t));
    if (first < count)
        memcpy(ring, src + first, (count - first) * sizeof(int16_t));
    *wpos = (pos + count) & RING_MASK;
}

static inline void ring_read(int16_t *ring, volatile int *rpos,
                             volatile int *avail, int16_t *dst, int count)
{
    int pos = *rpos;
    int first = (RING_SAMPLES - pos < count) ? RING_SAMPLES - pos : count;
    memcpy(dst, &ring[pos], first * sizeof(int16_t));
    if (first < count)
        memcpy(dst + first, ring, (count - first) * sizeof(int16_t));
    *rpos = (pos + count) & RING_MASK;
    *avail -= count;
}

/* ── Mic capture task ──────────────────────── */
static void mic_capture_task(void *arg)
{
    // PSRAM ring buffer: 65536 samples × 2 bytes = 128 KiB
    int16_t *ring = (int16_t *)heap_caps_calloc(RING_SAMPLES, sizeof(int16_t),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // Stack buffer for one I2S read (512 samples × 2 bytes = 1 KiB on stack — safe)
    int16_t read_buf[SAMPLES_PER_READ];
    // Stack buffer for one PCM frame
    int16_t frame_buf[SAMPLES_PER_FRAME];
    static uint8_t fbuf[PCM_FRAME_MAX_TOTAL];  // BSS (1928 bytes)

    if (!ring) {
        ESP_LOGE(TAG, "Failed to allocate ring buffer in PSRAM");
        vTaskDelete(NULL);
        return;
    }

    volatile int wpos = 0, rpos = 0, avail = 0;

    ESP_LOGI(TAG, "Mic capture started (voice_cmd pattern): ring=%p (%d samples PSRAM), "
             "read=%d samples × %d, frame=%d samples, buf=%p (stack) stack_hwm=%lu",
             (void *)ring, RING_SAMPLES,
             SAMPLES_PER_READ, READS_PER_CYCLE, SAMPLES_PER_FRAME,
             (void *)read_buf, uxTaskGetStackHighWaterMark(NULL));

    uint32_t cycle_ok = 0, read_fail = 0, frame_sent = 0;
    TickType_t last_report = xTaskGetTickCount();

    while (s_running && g_mic_handle) {
        // ── Read cycle: 2 × 512 samples (I2S mutex held during reads) ──
        bool read_error = false;
        if (s_i2s_mutex) xSemaphoreTake(s_i2s_mutex, pdMS_TO_TICKS(100));
        for (int r = 0; r < READS_PER_CYCLE; r++) {
            int ret = esp_codec_dev_read(g_mic_handle, read_buf, READ_BYTES);
            if (ret != 0) {
                read_fail++;
                read_error = true;
                vTaskDelay(pdMS_TO_TICKS(2));
                r--;  // retry this read
                continue;
            }
            ring_write(ring, &wpos, read_buf, SAMPLES_PER_READ);
            avail += SAMPLES_PER_READ;
        }
        if (s_i2s_mutex) xSemaphoreGive(s_i2s_mutex);
        if (read_error) continue;
        cycle_ok++;

        // ── Drain frames from ring while ≥ 960 samples available ──
        while (avail >= SAMPLES_PER_FRAME) {
            ring_read(ring, &rpos, &avail, frame_buf, SAMPLES_PER_FRAME);

            size_t flen = uart_frame_build_pcm(
                fbuf, sizeof(fbuf),
                UART_FRAME_PCM_UP, 0, s_seq++,
                frame_buf, SAMPLES_PER_FRAME);

            if (flen > 0) {
                uart_bridge_send_frame(UART_FRAME_PCM_UP, fbuf, flen);
                frame_sent++;
            }
        }

        // Periodic stats every 10 seconds
        TickType_t now = xTaskGetTickCount();
        if (now - last_report >= pdMS_TO_TICKS(10000)) {
            ESP_LOGI(TAG, "Mic stats: cycles=%lu frames=%lu fails=%lu "
                     "ring_avail=%d hwm=%lu",
                     cycle_ok, frame_sent, read_fail,
                     avail, uxTaskGetStackHighWaterMark(NULL));
            last_report = now;
        }
    }

    heap_caps_free(ring);
    ESP_LOGI(TAG, "Mic capture stopped: cycles=%lu frames=%lu fails=%lu final_hwm=%lu",
             cycle_ok, frame_sent, read_fail,
             uxTaskGetStackHighWaterMark(NULL));
    vTaskDelete(NULL);
}

/* ── Public API ───────────────────────────── */

void xiaozhi_audio_bridge_start(void)
{
    if (s_running) return;

    s_running = true;
    s_seq = 0;

    // Create I2S mutex (serialize TX + RX on shared I2S peripheral)
    if (!s_i2s_mutex) s_i2s_mutex = xSemaphoreCreateMutex();

    uart_bridge_on_frame(on_pcm_down_frame);

    BaseType_t ret = xTaskCreatePinnedToCore(mic_capture_task, "xz_mic",
        8192, NULL, 3, &s_mic_task, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create xz_mic task! Internal DRAM free: %lu",
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        s_running = false;
        return;
    }

    ESP_LOGI(TAG, "Xiaozhi audio bridge started (voice_cmd pattern, stack=8192, core=1, ret=%d)",
             (int)ret);
}

void xiaozhi_audio_bridge_stop(void)
{
    if (!s_running) return;

    s_running = false;

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
