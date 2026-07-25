/**
 * @file voice_verify.c
 * @brief PCM accumulation + HTTP POST to 3090 server for speaker verification.
 *
 * Called from uart_frame_demux_task after PCM_UP CRC validation.
 * Accumulates 50 frames (960 samples each = 3s) in PSRAM, then
 * HTTP POSTs raw int16 PCM to 3090 /verify endpoint.
 * Score is parsed from JSON response and sent back to P4 via CTRL_VOICE_SCORE.
 *
 * Bugs fixed:
 *   - Queue full: drop+buffer-free instead of overflow
 *   - Queue depth: increased to 4
 *   - Init guard: no-op if voice_verify_init() not yet called
 *   - CTRL buffer: uses CTRL_FRAME_MAX_TOTAL
 */
#include "voice_verify.h"
#include "xiaozhi_relay.h"
#include "uart_frame_protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "driver/uart.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "voice_verify";

#define FRAMES_PER_BATCH  50
#define SAMPLES_PER_FRAME 960
#define BUF_BYTES (FRAMES_PER_BATCH * SAMPLES_PER_FRAME * sizeof(int16_t))

/* ── Configurable server URL ── */
#ifndef VOICE_VERIFY_SERVER_URL
#define VOICE_VERIFY_SERVER_URL "http://10.53.101.96:8766/verify?speaker_id=user"
#endif

/* ── Local state ── */
static int16_t *s_accum        = NULL;
static int      s_count        = 0;
static QueueHandle_t s_queue   = NULL;

/* ── HTTP POST task ── */
#include "esp_radar.h"

static void http_post_task(void *arg)
{
    int16_t *batch;
    while (xQueueReceive(s_queue, &batch, portMAX_DELAY)) {
        /* Mute WiFi contention: stop CSI captures during critical HTTP window */
        esp_radar_stop();

        float score = 0.0f;
        char who[32] = "unknown";

        for (int attempt = 0; attempt < 3; attempt++) {
            esp_http_client_config_t cfg = {
                .url = VOICE_VERIFY_SERVER_URL,
                .method = HTTP_METHOD_POST,
                .timeout_ms = 8000,
            };
            esp_http_client_handle_t cli = esp_http_client_init(&cfg);
            if (!cli) break;

            esp_http_client_set_header(cli, "Content-Type", "application/octet-stream");

            bool ok = false;
            if (esp_http_client_open(cli, BUF_BYTES) == ESP_OK) {
                esp_http_client_write(cli, (const char *)batch, BUF_BYTES);
                if (esp_http_client_fetch_headers(cli) > 0) {
                    char resp[256] = {0};
                    int rlen = esp_http_client_read(cli, resp, sizeof(resp) - 1);
                    if (rlen > 0) {
                        resp[rlen] = 0;
                        const char *p = strstr(resp, "\"score\"");
                        if (p && (p = strchr(p, ':'))) score = strtof(p + 1, NULL);
                        p = strstr(resp, "\"speaker_id\"");
                        if (p) {
                            p = strchr(p, '"');
                            if (p) p = strchr(p + 1, '"');
                            if (p) { p++; int n = 0;
                                while (*p && *p != '"' && n < (int)sizeof(who)-1) who[n++] = *p++;
                                who[n] = 0; }
                        }
                        ESP_LOGI(TAG, "Score: %.4f who=%s", (double)score, who);
                        ok = true;
                    }
                }
            }
            esp_http_client_cleanup(cli);
            if (ok) break;
            if (attempt < 2) vTaskDelay(pdMS_TO_TICKS(500));
        }
        free(batch);

        /* Restore radar */
        esp_radar_start();

        /* Send score back to P4 */
        char js[96];
        int n = snprintf(js, sizeof(js), "{\"score\":%.4f,\"who\":\"%s\"}", (double)score, who);
        uint8_t ctrl[CTRL_FRAME_MAX_TOTAL];
        size_t clen = uart_frame_build_ctrl(ctrl, sizeof(ctrl),
                                             CTRL_VOICE_SCORE, js, (uint16_t)n);
        if (clen > 0) uart_write_bytes(UART_NUM_1, (const char *)ctrl, clen);
    }
    vTaskDelete(NULL);
}

/* ── Public API ── */

void voice_verify_init(void)
{
    if (s_queue) return;
    s_queue = xQueueCreate(4, sizeof(int16_t *));  // depth 4 (was 2)
    xTaskCreate(http_post_task, "verify_http", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "Init → %s", VOICE_VERIFY_SERVER_URL);
}

void voice_verify_feed_pcm(const int16_t *pcm_data, uint16_t sample_count)
{
    if (!s_queue) return;
    if (sample_count != SAMPLES_PER_FRAME) return;

    if (!s_accum) {
        s_accum = (int16_t *)heap_caps_malloc(BUF_BYTES, MALLOC_CAP_SPIRAM);
        if (!s_accum) return;
        s_count = 0;
    }

    memcpy(s_accum + s_count * SAMPLES_PER_FRAME, pcm_data,
           SAMPLES_PER_FRAME * sizeof(int16_t));
    s_count++;

    if (s_count >= FRAMES_PER_BATCH) {
        if (xQueueSend(s_queue, &s_accum, 0) == pdTRUE) {
            /* Ownership transferred to HTTP task — will be freed there */
            s_accum = NULL;
            s_count = 0;
        } else {
            /* Queue full: drop oldest batch, free, restart */
            ESP_LOGW(TAG, "Queue full, dropping batch");
            free(s_accum);
            s_accum = NULL;
            s_count = 0;
        }
    }
}
