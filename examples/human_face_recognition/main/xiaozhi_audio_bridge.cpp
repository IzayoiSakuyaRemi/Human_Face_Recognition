/**
 * @file xiaozhi_audio_bridge.cpp
 * @brief P4 audio bridge — speaker writes offloaded from UART callback.
 *
 * Mic: voice_cmd 512-sample pattern → PSRAM ring buffer → PCM_UP UART.
 * Speaker: PCM_DOWN frames enqueued in UART callback (FAST, no I2S blocking).
 * A dedicated speaker_task dequeues and writes to I2S TX.  This prevents
 * UART RX ring buffer overflow during I2S writes — root cause of "Frame too
 * large" desync bursts and dropped audio.
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
#include "freertos/queue.h"
#include <cstring>

static const char *TAG = "xz_audio";

extern esp_codec_dev_handle_t g_speaker_handle;
extern esp_codec_dev_handle_t g_mic_handle;

static SemaphoreHandle_t s_i2s_mutex = nullptr;

#define SAMPLES_PER_READ     512
#define READ_BYTES           (SAMPLES_PER_READ * sizeof(int16_t))
#define SAMPLES_PER_FRAME    960
#define PCM_BYTES            (SAMPLES_PER_FRAME * sizeof(int16_t))
#define READS_PER_CYCLE      2

#define RING_SAMPLES         65536
#define RING_MASK            (RING_SAMPLES - 1)

#define SPK_QUEUE_LEN        4

static TaskHandle_t s_mic_task = nullptr;
static TaskHandle_t s_spk_task = nullptr;
static QueueHandle_t s_spk_queue = nullptr;
static volatile bool s_running = false;
static uint16_t s_seq = 0;

/* ── Speaker task: dequeues PCM, writes to I2S ── */
static void speaker_task(void *arg)
{
    int16_t buf[SAMPLES_PER_FRAME];
    ESP_LOGI(TAG, "Speaker task started");

    while (s_running) {
        if (xQueueReceive(s_spk_queue, buf, pdMS_TO_TICKS(500)) != pdTRUE)
            continue;
        if (!g_speaker_handle || !s_i2s_mutex) continue;
        if (xSemaphoreTake(s_i2s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            esp_codec_dev_write(g_speaker_handle, buf, PCM_BYTES);
            xSemaphoreGive(s_i2s_mutex);
        }
    }
    vTaskDelete(NULL);
}

/* ── PCM_DOWN frame callback (UART RX task — MUST be <1ms) ── */
static void on_pcm_down_frame(uint8_t type, const uint8_t *data, size_t len)
{
    if (type != UART_FRAME_PCM_DOWN) return;

    pcm_frame_header_t hdr;
    const int16_t *pcm;
    uint16_t count;
    if (!uart_frame_parse_pcm(data, len, &hdr, &pcm, &count))
        return;

    // Fast enqueue only — no I2S, no ESP_LOGI in UART hot path
    if (count == SAMPLES_PER_FRAME && s_spk_queue) {
        xQueueSend(s_spk_queue, pcm, 0);
    }
}

/* ── Ring buffer helpers ──────────────────── */
static inline void ring_write(int16_t *ring, volatile int *wpos,
                              const int16_t *src, int count)
{
    if (!ring || !src || count <= 0) return;
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
    if (!ring || !dst || count <= 0 || *avail < count) return;
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
    int16_t *ring = (int16_t *)heap_caps_calloc(RING_SAMPLES, sizeof(int16_t),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t read_buf[SAMPLES_PER_READ];
    int16_t frame_buf[SAMPLES_PER_FRAME];
    static uint8_t fbuf[PCM_FRAME_MAX_TOTAL];

    if (!ring) { ESP_LOGE(TAG, "Ring alloc failed"); vTaskDelete(NULL); return; }

    volatile int wpos = 0, rpos = 0, avail = 0;

    ESP_LOGI(TAG, "Mic capture: ring=%p (%d samples PSRAM) stack_hwm=%lu",
             (void *)ring, RING_SAMPLES, uxTaskGetStackHighWaterMark(NULL));

    uint32_t cycle_ok = 0, read_fail = 0, frame_sent = 0;
    TickType_t last_report = xTaskGetTickCount();

    while (s_running && g_mic_handle) {
        bool read_error = false;
        if (s_i2s_mutex) xSemaphoreTake(s_i2s_mutex, pdMS_TO_TICKS(100));
        for (int r = 0; r < READS_PER_CYCLE; r++) {
            int ret = esp_codec_dev_read(g_mic_handle, read_buf, READ_BYTES);
            if (ret != 0) {
                read_fail++; read_error = true;
                vTaskDelay(pdMS_TO_TICKS(2)); r--;
                continue;
            }
            ring_write(ring, &wpos, read_buf, SAMPLES_PER_READ);
            avail += SAMPLES_PER_READ;
        }
        if (s_i2s_mutex) xSemaphoreGive(s_i2s_mutex);
        if (read_error) continue;
        cycle_ok++;

        int drain_iter = 0;
        while (avail >= SAMPLES_PER_FRAME && drain_iter++ < 16) {
            ring_read(ring, &rpos, &avail, frame_buf, SAMPLES_PER_FRAME);
            size_t flen = uart_frame_build_pcm(fbuf, sizeof(fbuf),
                UART_FRAME_PCM_UP, 0, s_seq++, frame_buf, SAMPLES_PER_FRAME);
            if (flen > 0) {
                uart_bridge_send_frame(UART_FRAME_PCM_UP, fbuf, flen);
                frame_sent++;
            }
        }

        TickType_t now = xTaskGetTickCount();
        if (now - last_report >= pdMS_TO_TICKS(10000)) {
            ESP_LOGI(TAG, "Mic: cycles=%lu frames=%lu fails=%lu avail=%d hwm=%lu",
                     cycle_ok, frame_sent, read_fail, avail,
                     uxTaskGetStackHighWaterMark(NULL));
            last_report = now;
        }
    }

    heap_caps_free(ring);
    ESP_LOGI(TAG, "Mic stopped: cycles=%lu frames=%lu fails=%lu hwm=%lu",
             cycle_ok, frame_sent, read_fail, uxTaskGetStackHighWaterMark(NULL));
    vTaskDelete(NULL);
}

/* ── Public API ───────────────────────────── */

void xiaozhi_audio_bridge_start(void)
{
    if (s_running) return;
    s_running = true;
    s_seq = 0;

    if (!s_i2s_mutex) s_i2s_mutex = xSemaphoreCreateMutex();

    // Speaker queue + task: offloads I2S TX from UART callback
    s_spk_queue = xQueueCreate(SPK_QUEUE_LEN, PCM_BYTES);
    if (s_spk_queue) {
        xTaskCreatePinnedToCore(speaker_task, "xz_spk", 4096, NULL, 2,
                                &s_spk_task, 1);
    }

    uart_bridge_on_frame(on_pcm_down_frame);

    BaseType_t ret = xTaskCreatePinnedToCore(mic_capture_task, "xz_mic",
        8192, NULL, 3, &s_mic_task, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create xz_mic task!");
        s_running = false;
        return;
    }

    ESP_LOGI(TAG, "Xiaozhi bridge started (mic=core1 spk=core1 queue=%d)",
             SPK_QUEUE_LEN);
}

void xiaozhi_audio_bridge_stop(void)
{
    if (!s_running) return;
    s_running = false;

    // Speaker task exits when s_running=false
    if (s_spk_task) {
        int wait_ms = 0;
        while (eTaskGetState(s_spk_task) != eDeleted && wait_ms < 2000) {
            vTaskDelay(pdMS_TO_TICKS(100)); wait_ms += 100;
        }
        if (wait_ms >= 2000) vTaskDelete(s_spk_task);
        s_spk_task = nullptr;
    }
    if (s_spk_queue) { vQueueDelete(s_spk_queue); s_spk_queue = nullptr; }

    if (s_mic_task) {
        int wait_ms = 0;
        while (eTaskGetState(s_mic_task) != eDeleted && wait_ms < 5000) {
            vTaskDelay(pdMS_TO_TICKS(100)); wait_ms += 100;
        }
        if (wait_ms >= 5000) vTaskDelete(s_mic_task);
        s_mic_task = nullptr;
    }

    ESP_LOGI(TAG, "Xiaozhi bridge stopped");
}

bool xiaozhi_audio_bridge_is_active(void)
{
    return s_running;
}
