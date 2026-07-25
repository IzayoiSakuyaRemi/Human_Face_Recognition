#include "uart_bridge.hpp"
#include "radar_display.hpp"
#include "xiaozhi/uart_frame_protocol.h"
#include "csi_liveness.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_brookesia.hpp"

static const char *TAG = "uart_brdg";

#define UART_PORT       UART_NUM_1
#define UART_TX_PIN     GPIO_NUM_4
#define UART_RX_PIN     GPIO_NUM_5
#define UART_BAUD       921600
#define UART_RX_BUF     8192  // 4 PCM frames @60ms each = 268ms buffer depth
#define UART_TX_Q_LEN   16
#define UART_BIN_TX_Q_LEN 8

/* ── Binary frame TX queue ─────────────────── */
typedef struct {
    uint8_t data[2048];
    size_t  len;
} bin_tx_msg_t;

static QueueHandle_t s_tx_queue = nullptr;
static QueueHandle_t s_bin_tx_queue = nullptr;

/* ── Frame callback registry (max 4) ────────── */
#define MAX_FRAME_CBS 4
static uart_frame_callback_t s_frame_cbs[MAX_FRAME_CBS] = {};
static int s_frame_cb_count = 0;

#define MAX_JSON_CBS 4
static uart_json_callback_t s_json_cbs[MAX_JSON_CBS] = {};
static int s_json_cb_count = 0;

/* ── Simple JSON value extractor (no cJSON dependency) ── */
static bool json_extract_str(const char *json, const char *key, char *out, size_t out_sz)
{
    char pat[64]; snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    const char *e = strchr(p, '"');
    if (!e || (size_t)(e - p) >= out_sz) return false;
    memcpy(out, p, e - p); out[e - p] = 0;
    return true;
}

static float json_extract_float(const char *json, const char *key)
{
    char pat[64]; snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return 0.0f;
    return strtof(p + strlen(pat), NULL);
}

/* ── TX task: text JSON (existing) ────────── */
static void uart_tx_task(void *arg)
{
    char buf[256];
    while (xQueueReceive(s_tx_queue, buf, portMAX_DELAY) == pdTRUE) {
        uart_write_bytes(UART_PORT, buf, strlen(buf));
        uart_write_bytes(UART_PORT, "\n", 1);
    }
    vTaskDelete(NULL);
}

/* ── Binary TX task: raw frame bytes ────────── */
static void uart_bin_tx_task(void *arg)
{
    bin_tx_msg_t msg;
    while (xQueueReceive(s_bin_tx_queue, &msg, portMAX_DELAY) == pdTRUE) {
        uart_write_bytes(UART_PORT, (const char *)msg.data, msg.len);
    }
    vTaskDelete(NULL);
}

/* ── RX task: frame-type-aware demux ────────── */
static void uart_rx_task(void *arg)
{
    uint8_t *data = (uint8_t *)malloc(UART_RX_BUF / 2);
    static char line[1024];
    static int line_pos = 0;
    static bool in_binary = false;
    static size_t bin_expected = 0;
    static size_t bin_pos = 0;
    static uint8_t bin_buf[2048];

    // Pre-validation: PCM audio data contains 0x03 bytes that trigger false
    // binary frame detection.  Buffer 6 bytes before committing to binary mode,
    // validate sample_count==960 to reject false positives.
    static bool    pcm_peeking = false;
    static uint8_t pcm_peek[6];
    static int     pcm_peek_pos = 0;

    if (!data) { vTaskDelete(NULL); return; }

    uint32_t loop_count = 0;
    uint32_t pcm_down_count = 0;
    uint32_t false_hdr_count = 0;
    TickType_t last_hwm_report = xTaskGetTickCount();

    ESP_LOGI(TAG, "RX task start: data=%p (heap) stack_hwm=%lu",
             (void *)data, uxTaskGetStackHighWaterMark(NULL));

    while (1) {
        loop_count++;
        int rx = uart_read_bytes(UART_PORT, data, UART_RX_BUF / 2 - 1,
                                 pdMS_TO_TICKS(200));
        if (rx <= 0) continue;

        for (int i = 0; i < rx; i++) {
            uint8_t byte = data[i];

            /* ── PCM_DOWN pre-validation: buffer 6-byte header ── */
            if (!in_binary && pcm_peeking) {
                pcm_peek[pcm_peek_pos++] = byte;
                if (pcm_peek_pos >= 6) {
                    pcm_peeking = false;
                    uint16_t sc = (uint16_t)pcm_peek[4] | ((uint16_t)pcm_peek[5] << 8);
                    if (sc == 960) {
                        // Valid PCM_DOWN header — enter binary mode pre-filled
                        in_binary = true;
                        bin_pos = 6;
                        memcpy(bin_buf, pcm_peek, 6);
                        bin_expected = 6 + 960 * 2 + 2;
                    } else {
                        // False positive — 0x03 was audio data, not a header
                        false_hdr_count++;
                        // Feed peeked bytes through text handler (they're harmless)
                        for (int j = 0; j < 6; j++) {
                            char tc = (char)pcm_peek[j];
                            if (tc == '\n' || tc == '\r') {
                                if (line_pos > 0) { line[line_pos]=0;
                                    for (int c=0;c<s_json_cb_count;c++) s_json_cbs[c](line);
                                    line_pos=0; }
                            } else if (line_pos < (int)sizeof(line)-1) {
                                line[line_pos++] = tc;
                            }
                        }
                    }
                }
                continue;
            }

            /* ── Binary frame detection ── */
            if (!in_binary && (byte == UART_FRAME_PCM_DOWN ||
                               byte == UART_FRAME_CTRL ||
                               byte == UART_FRAME_ADR018)) {
                // PCM_DOWN: pre-validate header before committing (audio data
                // contains 0x03 bytes that would otherwise trigger false frames)
                if (byte == UART_FRAME_PCM_DOWN) {
                    pcm_peeking = true;
                    pcm_peek_pos = 0;
                    pcm_peek[pcm_peek_pos++] = byte;
                    continue;
                }
                // CTRL and ADR018 enter binary mode immediately (low false-positive rate)
                if (line_pos > 0 && line[0] == '{') {
                    line[line_pos] = 0;
                    for (int c = 0; c < s_json_cb_count; c++)
                        s_json_cbs[c](line);
                }
                line_pos = 0;
                in_binary = true;
                bin_pos = 1;
                bin_buf[0] = byte;
                bin_expected = 2048;
                continue;
            }

            if (in_binary) {
                // CRITICAL: guard against malformed headers causing bin_buf overflow
                if (bin_pos >= (int)sizeof(bin_buf)) {
                    ESP_LOGW(TAG, "Binary frame overflow (%d bytes), aborting", (int)bin_pos);
                    in_binary = false;
                    continue;
                }
                bin_buf[bin_pos++] = byte;
                /* Try to determine expected length from header */
                if (bin_pos >= 6 && bin_buf[0] == UART_FRAME_PCM_DOWN) {
                    // PCM frame: header(6) + samples*2 + CRC(2)
                    uint16_t sc = (uint16_t)bin_buf[4] | ((uint16_t)bin_buf[5] << 8);
                    bin_expected = 6 + (size_t)sc * 2 + 2;
                } else if (bin_pos >= 4 && bin_buf[0] == UART_FRAME_CTRL) {
                    // Control frame: header(4) + payload_len
                    uint16_t pl = (uint16_t)bin_buf[2] | ((uint16_t)bin_buf[3] << 8);
                    bin_expected = 4 + pl;
                } else if (bin_pos >= 20 && bin_buf[0] == UART_FRAME_ADR018) {
                    // ADR-018: header(20) + IQ data (variable, max ~4100)
                    uint16_t nsub = (uint16_t)bin_buf[6] | ((uint16_t)bin_buf[7] << 8);
                    bin_expected = 20 + (size_t)nsub * 2;
                }
                // Cap bin_expected to buffer size (malformed header guard)
                if (bin_expected > sizeof(bin_buf)) {
                    ESP_LOGW(TAG, "Frame too large (%d > %d), discarding",
                             (int)bin_expected, (int)sizeof(bin_buf));
                    in_binary = false;
                    continue;
                }

                if (bin_pos >= bin_expected) {
                    /* Dispatch binary frame */
                    if (bin_buf[0] == UART_FRAME_PCM_DOWN) {
                        pcm_down_count++;
                        ESP_LOGI(TAG, "📥 PCM_DOWN dispatch: len=%d count=%lu loop=%lu hwm=%lu",
                                 (int)bin_pos, pcm_down_count, loop_count,
                                 uxTaskGetStackHighWaterMark(NULL));
                    }
                    for (int c = 0; c < s_frame_cb_count; c++)
                        s_frame_cbs[c](bin_buf[0], bin_buf, bin_pos);
                    if (bin_buf[0] == UART_FRAME_PCM_DOWN) {
                        ESP_LOGI(TAG, "📥 PCM_DOWN callback done: hwm=%lu",
                                 uxTaskGetStackHighWaterMark(NULL));
                    }
                    in_binary = false;
                    bin_pos = 0;
                }
                continue;
            }

            /* ── Text line handling ── */
            char c = (char)byte;
            if (c == '\n' || c == '\r') {
                if (line_pos > 0) {
                    line[line_pos] = 0;
                    if (line[0] == '{' && strstr(line, "\"dev\":\"s3\"") && strstr(line, "\"radar\"")) {
                        char room[12] = "?", move[12] = "?";
                        json_extract_str(line, "room", room, sizeof(room));
                        json_extract_str(line, "move", move, sizeof(move));
                        float w = json_extract_float(line, "wander");
                        float j = json_extract_float(line, "jitter");
                        radar_display_push(room, move, w, j);
                        csi_liveness_notify_room(room, move);
                        ESP_LOGD(TAG, "S3 radar: %s/%s w=%.4f j=%.4f", room, move, w, j);
                    }
                    // S3 time sync → set system clock directly (S3 has WiFi + SNTP)
                    if (line[0] == '{' && strstr(line, "\"dev\":\"s3\"") && strstr(line, "\"time\"")) {
                        const char *tp = strstr(line, "\"time\":");
                        long long ts = tp ? strtoll(tp + 7, NULL, 10) : 0;
                        if (ts > 1700000000) {  // sanity: must be after 2023
                            struct timeval tv = { .tv_sec = (time_t)ts, .tv_usec = 0 };
                            settimeofday(&tv, NULL);
                            ESP_LOGI(TAG, "Time set from S3: %lld", ts);
                        }
                    }
                    // S3 WiFi status → set flag, clock timer applies on Core1 (LVGL-safe)
                    if (line[0] == '{' && strstr(line, "\"dev\":\"s3\"") && strstr(line, "\"wifi\"")) {
                        extern volatile int g_wifi_icon_state;
                        g_wifi_icon_state = strstr(line, "\"connected\"") ? 3 : 0;
                    }
                    /* Forward to registered JSON callbacks */
                    for (int c = 0; c < s_json_cb_count; c++)
                        s_json_cbs[c](line);
                    line_pos = 0;
                }
            } else if (line_pos < (int)sizeof(line) - 1) {
                line[line_pos++] = c;
            }
        }

        // Periodic health report every 10 seconds
        TickType_t now = xTaskGetTickCount();
        if (now - last_hwm_report >= pdMS_TO_TICKS(10000)) {
            ESP_LOGI(TAG, "RX health: loops=%lu pcm_down=%lu false_hdr=%lu hwm=%lu",
                     loop_count, pcm_down_count, false_hdr_count,
                     uxTaskGetStackHighWaterMark(NULL));
            last_hwm_report = now;
        }
    }
    free(data);
    vTaskDelete(NULL);
}

/* ── Public API ── */

void uart_bridge_init(void)
{
    const uart_config_t cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // TX buffer MUST be > 1928 (max PCM frame) to avoid ring-buffer wrap bugs
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_RX_BUF, 4096, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    s_tx_queue     = xQueueCreate(UART_TX_Q_LEN, 256);
    s_bin_tx_queue = xQueueCreate(UART_BIN_TX_Q_LEN, sizeof(bin_tx_msg_t));
    xTaskCreatePinnedToCore(uart_tx_task,     "uart_tx",     3072, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(uart_bin_tx_task, "uart_bin_tx", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(uart_rx_task,     "uart_rx",     6144, NULL, 2, NULL, 0);

    // Register CSI liveness callback for ADR-018 frames (0xC5)
    uart_bridge_on_frame([](uint8_t type, const uint8_t *data, size_t len) {
        if (type == UART_FRAME_ADR018) csi_liveness_feed(data, len);
    });

    ESP_LOGI(TAG, "UART1 bridge: TX=GPIO%d RX=GPIO%d baud=%d (binary frames enabled)",
             UART_TX_PIN, UART_RX_PIN, UART_BAUD);
}

int uart_bridge_send(const char *data)
{
    if (!s_tx_queue) return 0;
    char buf[256];
    snprintf(buf, sizeof(buf), "{\"dev\":\"p4\",\"msg\":\"%s\"}", data);
    if (xQueueSend(s_tx_queue, buf, 0) != pdTRUE) return 0;
    return strlen(buf);
}

int uart_bridge_send_frame(uint8_t type, const uint8_t *data, size_t len)
{
    if (!s_bin_tx_queue || len > 2048) {
        ESP_LOGW(TAG, "Frame send fail: q=%p len=%d", s_bin_tx_queue, (int)len);
        return 0;
    }
    bin_tx_msg_t msg;
    memcpy(msg.data, data, len);
    msg.len = len;
    if (xQueueSend(s_bin_tx_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Frame TX queue full");
        return 0;
    }
    return (int)len;
}

void uart_bridge_on_frame(uart_frame_callback_t callback)
{
    if (s_frame_cb_count < MAX_FRAME_CBS)
        s_frame_cbs[s_frame_cb_count++] = callback;
}

void uart_bridge_on_json(uart_json_callback_t callback)
{
    if (s_json_cb_count < MAX_JSON_CBS)
        s_json_cbs[s_json_cb_count++] = callback;
}
