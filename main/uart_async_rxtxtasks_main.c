/* S3 CSI Radar — based on esp-radar console_test + UART1 bridge + SoftAP HTTP provisioning */

#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_partition.h"
#include "esp_http_server.h"
#include "argtable3/argtable3.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"
#include "mbedtls/base64.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_radar.h"
#include "esp_csi_gain_ctrl.h"
#include "csi_adr018.h"
#include "event_reporter.h"
#include "xiaozhi/uart_frame_protocol.h"
#include "xiaozhi/xiaozhi_relay.h"

/* ── C-linkage wrapper for UartAudioCodec (C++ class) ──
 * Called from this C file's demux task to feed PCM into the C++ codec. */
extern void uart_audio_codec_feed_pcm(const int16_t *data, int samples);

/* ── UART1 (to P4) ────────────────────────── */
#define TXD_PIN          GPIO_NUM_17
#define RXD_PIN          GPIO_NUM_18
#define UART_BAUD        921600
#define RX_BUF_SIZE      16384  // 8 PCM frames @60ms each = 537ms buffer depth

/* ── WiFi AP provisioning ─────────────────── */
#define AP_SSID          "S3-Config"
#define AP_CHANNEL        11

/* ── Radar ────────────────────────────────── */
#define SEND_DATA_FREQ    100
#define RADAR_BUFF_MAX    25
#define CSI_QUEUE_LEN     64

static const char *TAG = "s3";

/* ── Forward declarations ─────────────────── */
static void uart_init(void);
static void trigger_router_send_data_task(void *arg);
static void csi_data_print_task(void *arg);
static void cmd_register_radar(void);

/* ── Global state ─────────────────────────── */
static QueueHandle_t g_csi_queue       = NULL;
static bool g_wifi_connected           = false;
static uint32_t g_send_data_interval   = 20; /* 50 Hz, prevents esp_radar overflow with MGMT+DATA */
static esp_ping_handle_t g_ping_handle = NULL;

/* radar config (matches reference) */
static struct {
    bool train_start;
    float someone_threshold;
    float someone_sensitivity;
    float move_threshold;
    float move_sensitivity;
    uint32_t buff_size;
    uint32_t outliers;
    char collect_target[16];
    uint32_t collect_number;
    char csi_output_type[16];
    char csi_output_format[16];
} g_rcfg = {
    .someone_threshold   = 0.0f,
    .someone_sensitivity = 0.15f,
    .move_threshold      = 0.0003f,
    .move_sensitivity    = 0.20f,
    .buff_size           = 5,
    .outliers            = 2,
    .train_start         = false,
    .collect_target      = "unknown",
    .csi_output_type     = "LLTF",
    .csi_output_format   = "decimal",
};

static TimerHandle_t g_collect_timer = NULL;

/* ── CSI gain calibration ──────────────────── */
static bool g_gain_cali_done = false;

/* ── NVS radar config persistence ──────────── */
static void nvs_save_rcfg(void)
{
    nvs_handle_t h;
    if (nvs_open("radar_cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "rcfg", &g_rcfg, sizeof(g_rcfg));
        nvs_commit(h); nvs_close(h);
        ESP_LOGI(TAG, "Radar config saved to NVS");
    }
}

static void nvs_load_rcfg(void)
{
    nvs_handle_t h;
    if (nvs_open("radar_cfg", NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(g_rcfg);
        if (nvs_get_blob(h, "rcfg", &g_rcfg, &sz) == ESP_OK) {
            ESP_LOGI(TAG, "Radar config loaded from NVS: thr=%.6f/%.6f",
                     g_rcfg.someone_threshold, g_rcfg.move_threshold);
        }
        nvs_close(h);
    }
}

/* ── UART1 ─────────────────────────────────── */
static void uart_init(void)
{
    uart_config_t c = { .baud_rate = UART_BAUD, .data_bits = UART_DATA_8_BITS,
                        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
                        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT };
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE, 1024, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &c);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

/* ── Training state forwarded to P4 ─────────── */
static bool g_training_active = false;
static uint32_t g_training_start_ms = 0;

static void send_training_status(const char *status, int elapsed_s)
{
    char js[128];
    int n = snprintf(js, sizeof(js),
        "{\"training\":%s,\"elapsed_s\":%d,\"status\":\"%s\"}",
        g_training_active ? "true" : "false", elapsed_s, status);
    ctrl_frame_header_t hdr = {UART_FRAME_CTRL, CTRL_RADAR_STATUS, (uint16_t)n};
    uart_write_bytes(UART_NUM_1, (const char *)&hdr, 4);
    uart_write_bytes(UART_NUM_1, js, n);
}

static void handle_ctrl_train_start(void)
{
    esp_radar_train_remove();
    esp_radar_train_start();
    g_rcfg.train_start = true;
    g_training_active = true;
    g_training_start_ms = esp_log_timestamp();
    ESP_LOGI(TAG, "Radar training started (P4 command)");
    send_training_status("Training started", 0);
}

static void handle_ctrl_train_stop(void)
{
    esp_radar_train_stop(&g_rcfg.someone_threshold, &g_rcfg.move_threshold);
    g_rcfg.train_start = false;
    g_training_active = false;
    nvs_save_rcfg();
    ESP_LOGI(TAG, "Radar training stopped: someone=%.6f move=%.6f",
             g_rcfg.someone_threshold, g_rcfg.move_threshold);
    /* Send DONE with thresholds */
    char js[128];
    int n = snprintf(js, sizeof(js),
        "{\"someone_threshold\":%.6f,\"move_threshold\":%.6f}",
        g_rcfg.someone_threshold, g_rcfg.move_threshold);
    ctrl_frame_header_t hdr = {UART_FRAME_CTRL, CTRL_RADAR_TRAIN_DONE, (uint16_t)n};
    uart_write_bytes(UART_NUM_1, (const char *)&hdr, 4);
    uart_write_bytes(UART_NUM_1, js, n);
}

static void handle_ctrl_train_clear(void)
{
    esp_radar_train_remove();
    g_rcfg.train_start = false;
    g_training_active = false;
    g_rcfg.someone_threshold = 0.0f;
    g_rcfg.move_threshold = 0.0003f;
    nvs_save_rcfg();
    ESP_LOGI(TAG, "Radar training data cleared");
    send_training_status("Cleared", 0);
}

/* ── UART frame demux task (replaces rx_task) ── */
static void uart_frame_demux_task(void *arg)
{
    uint8_t *d = malloc(RX_BUF_SIZE + 1);
    static uint8_t bin_buf[2048];
    static size_t  bin_pos = 0;
    static bool    in_binary = false;
    static size_t  bin_expected = 0;

    uint32_t rx_total = 0;
    while (1) {
        int r = uart_read_bytes(UART_NUM_1, d, RX_BUF_SIZE, pdMS_TO_TICKS(100));
        if (r <= 0) continue;
        rx_total += r;
        /* Diagnostic: log every 5s */
        static uint32_t last_log = 0;
        if (esp_log_timestamp() - last_log > 5000) {
            ESP_LOGI(TAG, "UART1 RX: %d bytes this cycle, %lu total since boot, in_bin=%d pos=%d exp=%d",
                     r, rx_total, in_binary, (int)bin_pos, (int)bin_expected);
            last_log = esp_log_timestamp();
        }
        /* Log first byte of every chunk */
        if (r > 0) {
            ESP_LOGD(TAG, "UART chunk: %d bytes, first=0x%02X", r, d[0]);
        }

        for (int i = 0; i < r; i++) {
            uint8_t byte = d[i];

            /* Detect binary frame start — skip ADR-018 during xiaozhi mode */
            if (!in_binary && (byte == UART_FRAME_CTRL ||
                               byte == UART_FRAME_PCM_UP ||
                               (byte == UART_FRAME_ADR018 && !xiaozhi_relay_is_active()))) {
                bin_buf[0] = byte;
                bin_pos = 1;  /* position 0 filled, next byte goes to 1 */
                in_binary = true;
                bin_expected = 2048;
                continue;
            }

            if (in_binary) {
                /* Bounds check: never write past bin_buf[2048] */
                if (bin_pos >= sizeof(bin_buf)) {
                    ESP_LOGW(TAG, "Binary frame overflow (%d bytes), aborting", (int)bin_pos);
                    in_binary = false;
                    continue;
                }
                bin_buf[bin_pos++] = byte;
                ESP_LOGD(TAG, "Bin[%d]=0x%02X exp=%d",
                         (int)(bin_pos - 1), byte, (int)bin_expected);
                /* Determine expected length from header */
                if (bin_pos >= 4 && bin_buf[0] == UART_FRAME_CTRL) {
                    uint16_t pl = (uint16_t)bin_buf[2] | ((uint16_t)bin_buf[3] << 8);
                    if (pl > CTRL_FRAME_MAX_JSON) {
                        /* False CTRL frame — 0x04 in PCM data with garbage payload_len */
                        in_binary = false;
                        continue;
                    }
                    bin_expected = 4 + pl;
                } else if (bin_pos >= 6 && bin_buf[0] == UART_FRAME_PCM_UP) {
                    uint16_t sc = (uint16_t)bin_buf[4] | ((uint16_t)bin_buf[5] << 8);
                    if (sc != 960) {
                        /* False frame start — 0x02 appeared inside PCM data.
                         * Invalid sample_count. Abort binary mode immediately
                         * to avoid bin_buf overflow from huge bin_expected. */
                        in_binary = false;
                        continue;
                    }
                    bin_expected = 6 + (size_t)sc * 2 + 2;
                }

                if (bin_pos >= bin_expected) {
                    /* Dispatch */
                    uint8_t type = bin_buf[0];
                    if (type != UART_FRAME_PCM_UP)  // PCM_UP is too frequent (~17/sec)
                        ESP_LOGI(TAG, "Binary frame rx: type=0x%02X len=%d", type, (int)bin_pos);
                    else
                        ESP_LOGD(TAG, "Binary frame rx: type=0x%02X len=%d", type, (int)bin_pos);
                    if (type == UART_FRAME_CTRL) {
                        ctrl_frame_header_t *ctrl = (ctrl_frame_header_t *)bin_buf;
                        switch (ctrl->cmd) {
                        case CTRL_RADAR_TRAIN_START:
                            handle_ctrl_train_start(); break;
                        case CTRL_RADAR_TRAIN_STOP:
                            handle_ctrl_train_stop(); break;
                        case CTRL_RADAR_TRAIN_CLEAR:
                            handle_ctrl_train_clear(); break;
                        case CTRL_ENTER_XIAOZHI:
                        case CTRL_EXIT_XIAOZHI:
                            xiaozhi_relay_on_ctrl_from_p4(ctrl->cmd,
                                bin_buf + CTRL_FRAME_HEADER_SIZE,
                                ctrl->payload_len);
                            break;
                        default:
                            ESP_LOGI(TAG, "Unknown ctrl cmd %d", ctrl->cmd);
                        }
                    } else if (type == UART_FRAME_PCM_UP) {
                        /* P4→S3: raw PCM from microphone → feed UartAudioCodec */
                        pcm_frame_header_t pcm_hdr;
                        const int16_t *pcm_data;
                        uint16_t pcm_count;
                        if (uart_frame_parse_pcm(bin_buf, bin_pos, &pcm_hdr, &pcm_data, &pcm_count)) {
                            static uint32_t s_pcm_count = 0;
                            if (++s_pcm_count % 50 == 1)  // log every 50th frame (~3 sec)
                                ESP_LOGI(TAG, "PCM_UP #%lu: seq=%u samples=%u",
                                         s_pcm_count, pcm_hdr.seq, pcm_count);
                            uart_audio_codec_feed_pcm(pcm_data, pcm_count);
                        } else {
                            ESP_LOGW(TAG, "PCM_UP CRC mismatch, dropping frame");
                        }
                    } else if (type == UART_FRAME_ADR018) {
                        /* Existing ADR-018 radar binary (unchanged, routed during guard mode) */
                    }
                    in_binary = false;
                    bin_pos = 0;
                }
                continue;
            }

            /* Not in binary — byte goes to existing process_serial_rx_pkt */
            /* (unchanged — the existing serial/console handler still works) */
        }
    }
}

/* ── NVS helpers ───────────────────────────── */
static void nvs_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open("wifi_prov", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ssid", ssid); nvs_set_str(h, "pass", pass);
        nvs_commit(h); nvs_close(h);
    }
}

static bool nvs_load(char *ssid, size_t sl, char *pass, size_t pl)
{
    nvs_handle_t h;
    if (nvs_open("wifi_prov", NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t r = nvs_get_str(h, "ssid", ssid, &sl);
    if (r != ESP_OK) { nvs_close(h); return false; }
    r = nvs_get_str(h, "pass", pass, &pl);
    nvs_close(h);
    return (r == ESP_OK && strlen(ssid) >= 2);
}

/* ── CSI raw callback (→ queue) ───────────── */
static void wifi_csi_raw_cb(void *ctx, const wifi_csi_filtered_info_t *info)
{
    /* Gain calibration: record AGC/FFT samples for baseline */
    if (!g_gain_cali_done) {
        static int gain_cnt = 0;
        uint8_t agc; int8_t fft;
        esp_csi_gain_ctrl_get_rx_gain(info, &agc, &fft);
        esp_csi_gain_ctrl_record_rx_gain(agc, fft);
        if (++gain_cnt >= 100) {
            g_gain_cali_done = true;
            float comp;
            if (esp_csi_gain_ctrl_get_gain_compensation(&comp, agc, fft) == ESP_OK)
                ESP_LOGI(TAG, "CSI gain calibrated, compensation=%.2f", comp);
        }
    }

    /* ADR-018 binary frame → UART1 (50 Hz rate-limited, matching RuView) */
    {
        static int64_t s_last_uart_us = 0;
        int64_t now_us = esp_timer_get_time();
        if ((now_us - s_last_uart_us) >= 20 * 1000) {  /* 50 Hz, matches esp_radar rate */
            s_last_uart_us = now_us;
            uint8_t fbuf[8200];
            size_t flen = csi_serialize_adr018(
                (const wifi_csi_info_t *)info->info, /* raw info from esp_radar */
                0, fbuf, sizeof(fbuf));
            if (flen > 0) {
                uart_write_bytes(UART_NUM_1, fbuf, flen);
            }
        }
    }

    wifi_csi_filtered_info_t *q = malloc(sizeof(wifi_csi_filtered_info_t) + info->valid_len);
    if (!q) return;
    *q = *info;
    memcpy(q->valid_data, info->valid_data, info->valid_len);
    if (!g_csi_queue || xQueueSend(g_csi_queue, &q, 0) == pdFALSE) {
        ESP_LOGW(TAG, "CSI queue full");
        free(q);
    }
}

/* ── CSI data print task ──────────────────── */
static void csi_data_print_task(void *arg)
{
    wifi_csi_filtered_info_t *info = NULL;
    char *buf = malloc(8 * 1024);
    static uint32_t count = 0;
    if (!buf) { vTaskDelete(NULL); return; }

    while (xQueueReceive(g_csi_queue, &info, portMAX_DELAY)) {
        size_t len = 0;
        esp_radar_rx_ctrl_info_t *rx = &info->rx_ctrl_info;

        if (!count) {
            ESP_LOGI(TAG, "================ CSI RECV ================");
            len += sprintf(buf + len, "type,sequence,timestamp,taget_seq,target,mac,rssi,rate,sig_mode,mcs,bandwidth,smoothing,not_sounding,aggregation,stbc,fec_coding,sgi,noise_floor,ampdu_cnt,channel,secondary_channel,local_timestamp,ant,sig_len,rx_state,agc_gain,fft_gain,len,first_word,data\n");
        }

        uint16_t valid_len = info->valid_len;
        if (!strcasecmp(g_rcfg.csi_output_type, "LLTF"))
            info->valid_len = info->valid_lltf_len;
        else if (!strcasecmp(g_rcfg.csi_output_type, "HT-LTF"))
            info->valid_len = info->valid_lltf_len + info->valid_ht_ltf_len;
        else if (!strcasecmp(g_rcfg.csi_output_type, "STBC-HT-LTF"))
            info->valid_len = info->valid_lltf_len + info->valid_ht_ltf_len + info->valid_stbc_ht_ltf_len;
        if (info->valid_len == 0) info->valid_len = valid_len;

        len += sprintf(buf + len,
            "CSI_DATA,%d,%u,%u,%s," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%d,%d,%d,%d,",
            count++, esp_log_timestamp(), g_rcfg.collect_number, g_rcfg.collect_target,
            MAC2STR(info->mac), rx->rssi, rx->rate, rx->signal_mode,
            rx->mcs, rx->cwb, 0, 0, 0, rx->stbc, 0, 0,
            rx->noise_floor, 0, rx->channel, rx->secondary_channel,
            rx->timestamp, 0, 0, 0, rx->agc_gain, rx->fft_gain, info->valid_len, 0);

        if (!strcasecmp(g_rcfg.csi_output_format, "base64")) {
            size_t sz = 0;
            mbedtls_base64_encode((uint8_t *)buf + len, 8*1024 - len, &sz,
                                  (uint8_t *)info->valid_data, info->valid_len);
            len += sz;
            len += sprintf(buf + len, "\n");
        } else {
            len += sprintf(buf + len, "\"[%d", info->valid_data[0]);
            for (int i = 1; i < info->valid_len && len < 7800; i++)
                len += sprintf(buf + len, ",%d", info->valid_data[i]);
            len += sprintf(buf + len, "]\"\n");
        }
        printf("%s", buf);
        free(info);
    }
    free(buf);
    vTaskDelete(NULL);
}

/* ── Radar callback (matches reference exactly) ── */
static void wifi_radar_cb(void *ctx, const wifi_radar_info_t *info)
{
    static float *s_wander = NULL, *s_jitter = NULL;
    static uint32_t s_idx = 0;
    uint32_t bm = g_rcfg.buff_size, bo = g_rcfg.outliers;
    uint32_t sc = 0, mc = 0;

    if (!s_wander) s_wander = calloc(RADAR_BUFF_MAX, sizeof(float));
    if (!s_jitter) s_jitter = calloc(RADAR_BUFF_MAX, sizeof(float));

    s_wander[s_idx % RADAR_BUFF_MAX] = info->waveform_wander;
    s_jitter[s_idx % RADAR_BUFF_MAX] = info->waveform_jitter;
    s_idx++;
    if (s_idx < bm) return;

    extern float trimmean(const float *, size_t, float);
    extern float median(const float *, size_t);

    float wa = trimmean(s_wander, RADAR_BUFF_MAX, 0.5);
    float jm = median(s_jitter, RADAR_BUFF_MAX);

    for (int i = 0; i < bm; i++) {
        int idx = (s_idx - 1 - i) % RADAR_BUFF_MAX;
        if (wa * g_rcfg.someone_sensitivity > g_rcfg.someone_threshold) sc++;
        if (s_jitter[idx] * g_rcfg.move_sensitivity > g_rcfg.move_threshold
            || (s_jitter[idx] * g_rcfg.move_sensitivity > jm && s_jitter[idx] > 0.0002)) mc++;
    }
    bool room = (sc >= 1), human = (mc >= bo);

    static uint32_t s_count = 0, slm = 0, sls = 0, slp = 0, slt = 0;
    if (!s_count)
        ESP_LOGI(TAG, "================ RADAR RECV ================");

    if (g_rcfg.train_start) {
        slm = sls = esp_log_timestamp();
        /* Send training status via UART every 2s (not just silent return) */
        if (esp_log_timestamp() - slp >= 2000 && g_training_active) {
            slp = esp_log_timestamp();
            int elapsed = (esp_log_timestamp() - g_training_start_ms) / 1000;
            send_training_status("Collecting baseline data...", elapsed);
        }
        return;
    }

    /* State-change edge detection */
    static bool last_room = false, last_human = false;
    bool changed = (room != last_room || human != last_human);
    last_room = room; last_human = human;

    /* UART1 + HTTP: send radar status every 2 seconds */
    if (esp_log_timestamp() - slp >= 2000) {
        slp = esp_log_timestamp();
        const char *rs = room ? "OCCUPIED" : "EMPTY";
        const char *ms = human ? "MOVING" : "still";
        /* Console summary */
        printf("RADAR #%d  %s/%s  wander=%.4f jitter=%.4f\n", s_count++, rs, ms, info->waveform_wander, info->waveform_jitter);
        /* UART1 → P4 */
        char js[160];
        int n = snprintf(js, sizeof(js),
            "{\"dev\":\"s3\",\"radar\":{\"room\":\"%s\",\"move\":\"%s\",\"wander\":%.4f,\"jitter\":%.4f}}\n",
            rs, ms, info->waveform_wander, info->waveform_jitter);
        uart_write_bytes(UART_NUM_1, js, n);
        /* HTTP POST → Flask (only on state change to avoid flooding) */
        if (changed) report_radar_event(room, human, info->waveform_wander, info->waveform_jitter);
    }
    /* Debounce state changes: log once per 3 seconds */
    if (room) {
        if (human && esp_log_timestamp() - slm > 3000) { ESP_LOGI(TAG, ">>> MOVING <<<"); slm = esp_log_timestamp(); }
        if (!human && esp_log_timestamp() - slm > 3000) { ESP_LOGI(TAG, ">>> still <<<"); }
        sls = esp_log_timestamp();
    } else {
        if (human && esp_log_timestamp() - slt > 3000)  { ESP_LOGI(TAG, ">>> transient >>>"); slt = esp_log_timestamp(); }
        if (!human && esp_log_timestamp() - slm > 3000) { ESP_LOGI(TAG, ">>> no one <<<"); }
    }
}

/* ── trigger_router_send_data_task (matches reference) ── */
static void trigger_router_send_data_task(void *arg)
{
    esp_radar_config_t rcfg = {0};
    wifi_ap_record_t ap = {0};
    uint8_t smac[6] = {0};

    esp_radar_get_config(&rcfg);
    esp_wifi_sta_get_ap_info(&ap);
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, smac));

    rcfg.csi_config.csi_recv_interval = g_send_data_interval;
    memcpy(rcfg.csi_config.filter_dmac, smac, sizeof(rcfg.csi_config.filter_dmac));

    ESP_LOGI(TAG, "Send ping data to router");
    memcpy(rcfg.csi_config.filter_mac, ap.bssid, sizeof(rcfg.csi_config.filter_mac));
    esp_radar_change_config(&rcfg);

    if (g_ping_handle) {
        esp_ping_stop(g_ping_handle);
        esp_ping_delete_session(g_ping_handle);
        g_ping_handle = NULL;
    }

    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip);
    ESP_LOGI(TAG, "Ping: got ip:" IPSTR ", gw: " IPSTR, IP2STR(&ip.ip), IP2STR(&ip.gw));

    esp_ping_config_t pc = ESP_PING_DEFAULT_CONFIG();
    pc.count = 0; pc.data_size = 1; pc.interval_ms = g_send_data_interval;
    pc.target_addr.u_addr.ip4.addr = ip4_addr_get_u32(&ip.gw);
    pc.target_addr.type = ESP_IPADDR_TYPE_V4;
    esp_ping_callbacks_t cb = {0};
    esp_ping_new_session(&pc, &cb, &g_ping_handle);
    esp_ping_start(g_ping_handle);
    vTaskDelete(NULL);
}

/* ── WiFi event handler ────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t b, int32_t id, void *d)
{
    if (b == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        g_wifi_connected = true;
        xTaskCreate(trigger_router_send_data_task, "trig_send", 6144, NULL, 5, NULL);
        ESP_ERROR_CHECK(esp_wifi_set_promiscuous(false));
    } else if (b == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        g_wifi_connected = false;
        ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting...");
        if (g_ping_handle) { esp_ping_stop(g_ping_handle); esp_ping_delete_session(g_ping_handle); g_ping_handle = NULL; }
        /* Simple reconnect instead of full reinit (reinit breaks STA netif) */
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
    }
}

/* ── collect timer ─────────────────────────── */
static void collect_timercb(TimerHandle_t t)
{
    if (!--g_rcfg.collect_number) {
        xTimerStop(g_collect_timer, 0);
        xTimerDelete(g_collect_timer, 0);
        g_collect_timer = NULL;
        strcpy(g_rcfg.collect_target, "unknown");
    }
}

/* ══════════════════════════════════════════════
 *  CONSOLE COMMANDS
 * ══════════════════════════════════════════════ */

/* ── radar (full reference clone) ──────────── */
static struct {
    struct arg_lit *train_start, *train_stop, *train_add, *train_clear;
    struct arg_str *someone_thr, *someone_sens, *move_thr, *move_sens;
    struct arg_int *buff_size, *outliers;
    struct arg_str *collect_target;
    struct arg_int *collect_num, *collect_dur;
    struct arg_lit *csi_start, *csi_stop;
    struct arg_str *csi_type, *csi_format;
    struct arg_int *scale_shift, *channel_filter, *send_interval;
    struct arg_end *end;
} radar_args;

static int cmd_radar(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **)&radar_args) != ESP_OK) {
        arg_print_errors(stderr, radar_args.end, argv[0]);
        return ESP_FAIL;
    }
    if (radar_args.train_start->count) {
        if (!radar_args.train_add->count) esp_radar_train_remove();
        esp_radar_train_start();
        g_rcfg.train_start = true;
    }
    if (radar_args.train_clear->count) {
        esp_radar_train_remove();
        g_rcfg.train_start = false;
        g_rcfg.someone_threshold = 0.0f;
        g_rcfg.move_threshold = 0.0003f;
        nvs_save_rcfg();
        printf("Training data cleared, thresholds reset to defaults\n");
    }
    if (radar_args.train_stop->count) {
        esp_radar_train_stop(&g_rcfg.someone_threshold, &g_rcfg.move_threshold);
        g_rcfg.train_start = false;
        nvs_save_rcfg();
        printf("RADAR_DADA,0,0,0,%.6f,0,0,%.6f,0\n",
               g_rcfg.someone_threshold, g_rcfg.move_threshold);
    }
    if (radar_args.move_thr->count) {
        g_rcfg.move_threshold = atof(radar_args.move_thr->sval[0]);
        if (!g_rcfg.train_start) nvs_save_rcfg();
    }
    if (radar_args.move_sens->count) {
        g_rcfg.move_sensitivity = atof(radar_args.move_sens->sval[0]);
        ESP_LOGI(TAG, "move_sensitivity: %f", g_rcfg.move_sensitivity);
        if (!g_rcfg.train_start) nvs_save_rcfg();
    }
    if (radar_args.someone_thr->count) {
        g_rcfg.someone_threshold = atof(radar_args.someone_thr->sval[0]);
        if (!g_rcfg.train_start) nvs_save_rcfg();
    }
    if (radar_args.someone_sens->count) {
        g_rcfg.someone_sensitivity = atof(radar_args.someone_sens->sval[0]);
        ESP_LOGI(TAG, "someone_sensitivity: %f", g_rcfg.someone_sensitivity);
        if (!g_rcfg.train_start) nvs_save_rcfg();
    }
    if (radar_args.buff_size->count) {
        g_rcfg.buff_size = radar_args.buff_size->ival[0];
        if (!g_rcfg.train_start) nvs_save_rcfg();
    }
    if (radar_args.outliers->count) {
        g_rcfg.outliers = radar_args.outliers->ival[0];
        if (!g_rcfg.train_start) nvs_save_rcfg();
    }

    if (radar_args.collect_target->count && radar_args.collect_num->count && radar_args.collect_dur->count) {
        g_rcfg.collect_number = radar_args.collect_num->ival[0];
        strcpy(g_rcfg.collect_target, radar_args.collect_target->sval[0]);
        if (g_collect_timer) { xTimerStop(g_collect_timer, portMAX_DELAY); xTimerDelete(g_collect_timer, portMAX_DELAY); }
        g_collect_timer = xTimerCreate("collect", pdMS_TO_TICKS(radar_args.collect_dur->ival[0]), true, NULL, collect_timercb);
        xTimerStart(g_collect_timer, portMAX_DELAY);
    }

    if (radar_args.csi_format->count)
        strcpy(g_rcfg.csi_output_format, radar_args.csi_format->sval[0]);

    if (radar_args.csi_type->count) {
        esp_radar_config_t rcfg = {0};
        esp_radar_get_config(&rcfg);
        if (!strcasecmp(radar_args.csi_type->sval[0], "NULL")) {
            rcfg.csi_config.csi_filtered_cb = NULL;
        } else {
            rcfg.csi_config.csi_filtered_cb = wifi_csi_raw_cb;
            strcpy(g_rcfg.csi_output_type, radar_args.csi_type->sval[0]);
#if CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C61
            if (!strcasecmp(radar_args.csi_type->sval[0], "LLTF")) {
                rcfg.csi_config.acquire_csi_lltf = true;
                rcfg.dec_config.ltf_type = RADAR_LTF_TYPE_LLTF;
                rcfg.dec_config.sub_carrier_step_size = 2;
            } else if (!strcasecmp(radar_args.csi_type->sval[0], "HT-LTF")) {
                rcfg.csi_config.acquire_csi_lltf = false;
                rcfg.csi_config.acquire_csi_ht20 = true;
                rcfg.csi_config.acquire_csi_ht40 = true;
                rcfg.csi_config.acquire_csi_vht = true;
                rcfg.dec_config.ltf_type = RADAR_LTF_TYPE_HTLTF;
                rcfg.dec_config.sub_carrier_step_size = 5;
            } else if (!strcasecmp(radar_args.csi_type->sval[0], "STBC-HT-LTF")) {
                rcfg.csi_config.acquire_csi_lltf = false;
                rcfg.csi_config.acquire_csi_ht20 = true;
                rcfg.csi_config.acquire_csi_ht40 = true;
                rcfg.csi_config.acquire_csi_vht = true;
                rcfg.dec_config.ltf_type = RADAR_LTF_TYPE_STBC_HTLTF;
                rcfg.dec_config.sub_carrier_step_size = 5;
            }
#endif
        }
        esp_radar_change_config(&rcfg);
    }

    if (radar_args.csi_start->count) esp_radar_start();
    if (radar_args.csi_stop->count) esp_radar_stop();

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32
    if (radar_args.scale_shift->count) {
        esp_radar_config_t rcfg = {0};
        esp_radar_get_config(&rcfg);
        rcfg.csi_config.shift = radar_args.scale_shift->ival[0];
        esp_radar_change_config(&rcfg);
        ESP_LOGI(TAG, "scale_shift: %d", (int)rcfg.csi_config.shift);
    }
    if (radar_args.channel_filter->count) {
        esp_radar_config_t rcfg = {0};
        esp_radar_get_config(&rcfg);
        rcfg.csi_config.channel_filter_en = radar_args.channel_filter->ival[0];
        esp_radar_change_config(&rcfg);
        ESP_LOGI(TAG, "channel_filter: %d", (int)rcfg.csi_config.channel_filter_en);
    }
#endif
    if (radar_args.send_interval->count)
        g_send_data_interval = radar_args.send_interval->ival[0];

    return ESP_OK;
}

static void cmd_register_radar(void)
{
    radar_args.train_start  = arg_lit0(NULL, "train_start", "Start calibrating the Radar algorithm");
    radar_args.train_stop   = arg_lit0(NULL, "train_stop", "Stop calibrating the Radar algorithm");
    radar_args.train_add    = arg_lit0(NULL, "train_add", "Calibrate on the basis of saving the calibration results");
    radar_args.train_clear  = arg_lit0(NULL, "train_clear", "Remove all calibration data");
    radar_args.someone_thr  = arg_str0(NULL, "predict_someone_threshold", "<0~1.0>", "Configure the threshold for someone");
    radar_args.someone_sens = arg_str0(NULL, "predict_someone_sensitivity", "<0~1.0>", "Configure the sensitivity for someone");
    radar_args.move_thr     = arg_str0(NULL, "predict_move_threshold", "<0~1.0>", "Configure the threshold for move");
    radar_args.move_sens    = arg_str0(NULL, "predict_move_sensitivity", "<0~1.0>", "Configure the sensitivity for move");
    radar_args.buff_size    = arg_int0(NULL, "predict_buff_size", "<1~100>", "Buffer size for filtering outliers");
    radar_args.outliers     = arg_int0(NULL, "predict_outliers_number", "<1~100>", "Number of items in the buffer queue greater than the threshold");
    radar_args.collect_target = arg_str0(NULL, "collect_tagets", "<tag>", "Type of CSI data collected");
    radar_args.collect_num    = arg_int0(NULL, "collect_number", "<N>", "Number of times CSI data was collected");
    radar_args.collect_dur    = arg_int0(NULL, "collect_duration", "<ms>", "Time taken to acquire one CSI data");
    radar_args.csi_start    = arg_lit0(NULL, "csi_start", "Start collecting CSI data from Wi-Fi");
    radar_args.csi_stop     = arg_lit0(NULL, "csi_stop", "Stop CSI data collection from Wi-Fi");
    radar_args.csi_type     = arg_str0(NULL, "csi_output_type", "<NULL|LLTF|HT-LTF|STBC-HT-LTF>", "Type of CSI data");
    radar_args.csi_format   = arg_str0(NULL, "csi_output_format", "<decimal|base64>", "Format of CSI data");
    radar_args.scale_shift  = arg_int0(NULL, "scale_shift", "<0~15>", "manually left shift bits of the scale of the CSI data");
    radar_args.channel_filter = arg_int0(NULL, "channel_filter", "<0|1>", "enable to turn on channel filter to smooth adjacent sub-carrier");
    radar_args.send_interval  = arg_int0(NULL, "send_data_interval", "<ms>", "The interval between sending ping packets to the router");
    radar_args.end = arg_end(8);
    const esp_console_cmd_t cmd = { .command = "radar", .help = "Radar config", .func = &cmd_radar, .argtable = &radar_args };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ── wifi_config (reference clone) ─────────── */
static struct {
    struct arg_str *ssid, *password, *bssid;
    struct arg_int *channel, *channel_second, *tx_power, *rate, *bandwidth, *protocol;
    struct arg_str *country_code;
    struct arg_lit *disconnect, *info;
    struct arg_end *end;
} wifi_cfg_args;

static bool mac_str2hex(const char *s, uint8_t *h)
{
    unsigned d[6];
    if (sscanf(s, "%02x:%02x:%02x:%02x:%02x:%02x", d,d+1,d+2,d+3,d+4,d+5) != 6) return false;
    for (int i = 0; i < 6; i++) h[i] = (uint8_t)d[i];
    return true;
}

static int cmd_wifi_config(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **)&wifi_cfg_args) != ESP_OK) {
        arg_print_errors(stderr, wifi_cfg_args.end, argv[0]);
        return ESP_FAIL;
    }
    wifi_config_t wc = {0};
    esp_wifi_get_config(WIFI_IF_STA, &wc);

    if (wifi_cfg_args.disconnect->count) { esp_wifi_disconnect(); return ESP_OK; }
    if (wifi_cfg_args.ssid->count)
        snprintf((char*)wc.sta.ssid, sizeof(wc.sta.ssid), "%s", wifi_cfg_args.ssid->sval[0]);
    if (wifi_cfg_args.password->count)
        snprintf((char*)wc.sta.password, sizeof(wc.sta.password), "%s", wifi_cfg_args.password->sval[0]);
    if (wifi_cfg_args.bssid->count) {
        if (!mac_str2hex(wifi_cfg_args.bssid->sval[0], wc.sta.bssid)) {
            ESP_LOGE(TAG, "Bad BSSID format, use xx:xx:xx:xx:xx:xx");
            return ESP_FAIL;
        }
    }
    if (strlen((char*)wc.sta.ssid)) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &wc);
        esp_wifi_connect();
    }
    if (wifi_cfg_args.country_code->count) {
        wifi_country_t cc = {0};
        const char *c = wifi_cfg_args.country_code->sval[0];
        if (!strcasecmp(c, "CN"))      { strcpy(cc.cc, "CN"); cc.schan = 1; cc.nchan = 13; }
        else if (!strcasecmp(c, "US")) { strcpy(cc.cc, "US"); cc.schan = 1; cc.nchan = 11; }
        else if (!strcasecmp(c, "JP")) { strcpy(cc.cc, "JP"); cc.schan = 1; cc.nchan = 14; }
        esp_wifi_set_country(&cc);
    }
    if (wifi_cfg_args.channel->count)
        esp_wifi_set_channel(wifi_cfg_args.channel->ival[0],
            wifi_cfg_args.channel_second->count ? wifi_cfg_args.channel_second->ival[0] : WIFI_SECOND_CHAN_NONE);
    if (wifi_cfg_args.tx_power->count) esp_wifi_set_max_tx_power(wifi_cfg_args.tx_power->ival[0]);
    if (wifi_cfg_args.bandwidth->count) esp_wifi_set_bandwidth(WIFI_IF_STA, wifi_cfg_args.bandwidth->ival[0]);
    if (wifi_cfg_args.protocol->count) esp_wifi_set_protocol(WIFI_IF_STA, wifi_cfg_args.protocol->ival[0]);
    if (wifi_cfg_args.rate->count) {
        extern esp_err_t esp_wifi_internal_set_fix_rate(wifi_interface_t ifx, bool en, wifi_phy_rate_t rate);
        esp_wifi_internal_set_fix_rate(WIFI_IF_STA, true, wifi_cfg_args.rate->ival[0]);
    }
    if (wifi_cfg_args.info->count) {
        int8_t tp = 0; wifi_country_t cc = {0};
        uint8_t ch = 0; wifi_second_chan_t sc = 0;
        wifi_mode_t m = 0; wifi_bandwidth_t bw = 0;
        esp_wifi_get_channel(&ch, &sc); esp_wifi_get_max_tx_power(&tp);
        esp_wifi_get_country(&cc); esp_wifi_get_mode(&m);
        esp_wifi_get_bandwidth(m - 1, &bw);
        ESP_LOGI(TAG, "tx_power:%d country:%s ch:%d/%d mode:%d bw:%d", tp, cc.cc, ch, sc, m, bw);
    }
    /* Save to NVS */
    if (wifi_cfg_args.ssid->count) {
        nvs_save(wifi_cfg_args.ssid->sval[0],
                 wifi_cfg_args.password->count ? wifi_cfg_args.password->sval[0] : "");
    }
    return ESP_OK;
}

static void cmd_register_wifi_config(void)
{
    wifi_cfg_args.ssid     = arg_str0("s", "ssid", "<ssid>", "SSID of router");
    wifi_cfg_args.password = arg_str0("p", "password", "<password>", "Password of router");
    wifi_cfg_args.bssid    = arg_str0("b", "bssid", "<xx:xx:xx:xx:xx:xx>", "BSSID of router");
    wifi_cfg_args.disconnect = arg_lit0("d", "disconnect", "Disconnect the router");
    wifi_cfg_args.channel  = arg_int0("c", "channel", "<1~14>", "Set primary channel");
    wifi_cfg_args.channel_second = arg_int0("c", "channel_second", "<0|1|2>", "Set second channel");
    wifi_cfg_args.country_code = arg_str0("C", "country_code", "<CN|JP|US>", "Set country code");
    wifi_cfg_args.tx_power = arg_int0("t", "tx_power", "<8~84>", "Set max TX power");
    wifi_cfg_args.rate     = arg_int0("r", "rate", "<MCS>", "Set fixed rate");
    wifi_cfg_args.bandwidth = arg_int0("w", "bandwidth", "<1:HT20 2:HT40>", "Set bandwidth");
    wifi_cfg_args.protocol = arg_int0("P", "protocol", "<bgn>", "Set protocol");
    wifi_cfg_args.info     = arg_lit0("i", "info", "Get Wi-Fi configuration information");
    wifi_cfg_args.end      = arg_end(10);
    const esp_console_cmd_t cmd = { .command = "wifi_config", .help = "Set the configuration of the ESP32 STA", .func = &cmd_wifi_config, .argtable = &wifi_cfg_args };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ── wifi_scan (reference clone) ───────────── */
static struct {
    struct arg_int *rssi;
    struct arg_str *ssid, *bssid;
    struct arg_int *passive;
    struct arg_end *end;
} ws_args;

static int cmd_wifi_scan(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **)&ws_args) != ESP_OK) {
        arg_print_errors(stderr, ws_args.end, argv[0]); return ESP_FAIL;
    }
    int8_t fr = ws_args.rssi->count ? ws_args.rssi->ival[0] : -120;
    wifi_scan_config_t sc = { .show_hidden = true, .scan_type = WIFI_SCAN_TYPE_ACTIVE };
    if (ws_args.passive->count) { sc.scan_type = WIFI_SCAN_TYPE_PASSIVE; sc.scan_time.passive = ws_args.passive->ival[0]; }
    if (ws_args.ssid->count) sc.ssid = (uint8_t *)ws_args.ssid->sval[0];
    if (ws_args.bssid->count) { uint8_t b[6]; if (mac_str2hex(ws_args.bssid->sval[0], b)) sc.bssid = b; }

    uint16_t n = 0; uint8_t ch = 0; wifi_second_chan_t sc2 = 0;
    esp_wifi_get_channel(&ch, &sc2);
    esp_wifi_scan_stop(); esp_wifi_disconnect();
    int retry = 20;
    do { esp_wifi_scan_start(&sc, true); esp_wifi_scan_get_ap_num(&n); } while (n <= 0 && --retry);
    ESP_LOGI(TAG, "Found %d APs", n);
    wifi_ap_record_t *aps = calloc(n, sizeof(wifi_ap_record_t));
    if (aps) {
        esp_wifi_scan_get_ap_records(&n, aps);
        for (int i = 0; i < n; i++) {
            if (aps[i].rssi < fr) continue;
            ESP_LOGI(TAG, "  %s  " MACSTR "  ch:%u  rssi:%d", aps[i].ssid, MAC2STR(aps[i].bssid), aps[i].primary, aps[i].rssi);
        }
        free(aps);
    }
    if (ch > 0 && ch < 13) esp_wifi_set_channel(ch, sc2);
    return ESP_OK;
}

static void cmd_register_wifi_scan(void)
{
    ws_args.rssi    = arg_int0("r", "rssi", "<-120~0>", "Filter device uses RSSI");
    ws_args.ssid    = arg_str0("s", "ssid", "<ssid>", "Filter device uses SSID");
    ws_args.bssid   = arg_str0("b", "bssid", "<xx:xx:xx:xx:xx:xx>", "Filter device uses AP's MAC");
    ws_args.passive = arg_int0("p", "passive", "<ms>", "Passive scan time per channel");
    ws_args.end     = arg_end(5);
    const esp_console_cmd_t cmd = { .command = "wifi_scan", .help = "Wi-Fi scan", .func = &cmd_wifi_scan, .argtable = &ws_args };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ── ping ──────────────────────────────────── */
static struct {
    struct arg_int *timeout, *interval, *data_size, *count, *tos;
    struct arg_str *host;
    struct arg_lit *abort;
    struct arg_end *end;
} ping_args;

static void ping_on_success(esp_ping_handle_t h, void *a) {
    uint16_t seq; uint32_t t; esp_ping_get_profile(h, ESP_PING_PROF_SEQNO, &seq, sizeof(seq));
    esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &t, sizeof(t));
    ESP_LOGD(TAG, "ping: seq=%d time=%dms", seq, (int)t);
}
static void ping_on_end(esp_ping_handle_t h, void *a) {
    uint32_t tx=0, rx=0, tt=0; esp_ping_get_profile(h, ESP_PING_PROF_REQUEST, &tx, sizeof(tx));
    esp_ping_get_profile(h, ESP_PING_PROF_REPLY, &rx, sizeof(rx));
    esp_ping_get_profile(h, ESP_PING_PROF_DURATION, &tt, sizeof(tt));
    ESP_LOGI(TAG, "Ping: %d sent, %d recv, %dms", (int)tx, (int)rx, (int)tt);
    esp_ping_delete_session(h);
}

static int cmd_ping(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **)&ping_args) != ESP_OK) { arg_print_errors(stderr, ping_args.end, argv[0]); return ESP_FAIL; }
    esp_ping_config_t pc = ESP_PING_DEFAULT_CONFIG();
    pc.count = ping_args.count->count ? ping_args.count->ival[0] : 0;
    pc.data_size = ping_args.data_size->count ? ping_args.data_size->ival[0] : 1;
    pc.interval_ms = ping_args.interval->count ? ping_args.interval->ival[0] : 10;
    if (ping_args.timeout->count) pc.timeout_ms = ping_args.timeout->ival[0];
    if (ping_args.tos->count) pc.tos = ping_args.tos->ival[0];

    if (ping_args.abort->count) {
        if (g_ping_handle) { esp_ping_stop(g_ping_handle); esp_ping_delete_session(g_ping_handle); g_ping_handle = NULL; }
        return ESP_OK;
    }
    char host[32] = {0};
    if (ping_args.host->count) snprintf(host, sizeof(host), "%s", ping_args.host->sval[0]);
    else {
        esp_netif_ip_info_t ip;
        esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip);
        snprintf(host, sizeof(host), IPSTR, IP2STR(&ip.gw));
    }
    struct sockaddr_in6 sa6; ip_addr_t ta; memset(&ta, 0, sizeof(ta));
    if (inet_pton(AF_INET6, host, &sa6.sin6_addr) == 1) ipaddr_aton(host, &ta);
    else {
        struct addrinfo h, *r = NULL; memset(&h, 0, sizeof(h));
        if (getaddrinfo(host, NULL, &h, &r) != 0) { printf("ping: unknown host %s\n", host); return ESP_FAIL; }
        if (r->ai_family == AF_INET) { struct in_addr a4 = ((struct sockaddr_in *)(r->ai_addr))->sin_addr; inet_addr_to_ip4addr(ip_2_ip4(&ta), &a4); ta.type = IPADDR_TYPE_V4; }
        else { struct in6_addr a6 = ((struct sockaddr_in6 *)(r->ai_addr))->sin6_addr; inet6_addr_to_ip6addr(ip_2_ip6(&ta), &a6); ta.type = IPADDR_TYPE_V6; }
        freeaddrinfo(r);
    }
    pc.target_addr = ta;
    esp_ping_callbacks_t cbs = { .on_ping_success = ping_on_success, .on_ping_end = ping_on_end };
    esp_ping_new_session(&pc, &cbs, &g_ping_handle);
    esp_ping_start(g_ping_handle);
    return ESP_OK;
}

static void cmd_register_ping(void)
{
    ping_args.timeout = arg_int0("W", "timeout", "<t>", "Time to wait for a response, in seconds");
    ping_args.interval = arg_int0("i", "interval", "<t>", "Wait interval seconds between sending each packet");
    ping_args.data_size = arg_int0("s", "size", "<n>", "Specify the number of data bytes to be sent");
    ping_args.count = arg_int0("c", "count", "<n>", "Stop after sending count packets");
    ping_args.tos = arg_int0("Q", "tos", "<n>", "Set Type of Service related bits in IP datagrams");
    ping_args.host = arg_str0("h", "host", "<host>", "Host address");
    ping_args.abort = arg_lit0("a", "abort", "Abort running ping");
    ping_args.end = arg_end(10);
    const esp_console_cmd_t cmd = { .command = "ping", .help = "Send ICMP ECHO_REQUEST to network hosts", .func = &cmd_ping, .argtable = &ping_args };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ── system commands ───────────────────────── */
static int cmd_version(int argc, char **argv) {
    esp_chip_info_t ci; esp_chip_info(&ci);
    printf("IDF: %s  Cores: %d  Rev: %d  Free heap: %d\n", "v5.5.4", ci.cores, ci.revision, (int)esp_get_free_heap_size());
    return 0;
}
static int cmd_restart(int argc, char **argv) { esp_restart(); return 0; }
static int cmd_reset(int argc, char **argv) {
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "nvs");
    const esp_partition_t *p = esp_partition_get(it);
    esp_partition_erase_range(p, 0, p->size);
    esp_restart();
}
static struct { struct arg_str *tag, *level; struct arg_end *end; } log_args;
static int cmd_log(int argc, char **argv) {
    if (arg_parse(argc, argv, (void **)&log_args) != ESP_OK) { arg_print_errors(stderr, log_args.end, argv[0]); return ESP_FAIL; }
    const char *lvls[] = {"NONE","ERR","WARN","INFO","DEBUG","VER"};
    for (int i = 0; log_args.level->count && i < 6; i++)
        if (!strncasecmp(lvls[i], log_args.level->sval[0], strlen(lvls[i])))
            esp_log_level_set(log_args.tag->count ? log_args.tag->sval[0] : "*", i);
    return 0;
}

/* ── Our SoftAP HTTP portal ────────────────── */
static httpd_handle_t g_httpd = NULL;
static char g_cfg_ssid[33] = {0}, g_cfg_pass[65] = {0};

static const char *HTML_PAGE = R"raw(<!DOCTYPE html>
<html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>S3 WiFi Setup</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#eee;display:flex;justify-content:center;align-items:center;min-height:100vh;padding:16px}
.c{background:#16213e;border-radius:12px;padding:28px 22px;max-width:380px;width:100%;box-shadow:0 6px 24px rgba(0,0,0,.35)}
h2{text-align:center;color:#e94560;margin:0 0 22px;font-size:20px}
label{display:block;margin:14px 0 5px;color:#a0a0b0;font-size:13px}
select,input{width:100%;padding:10px;border:1px solid #0f3460;border-radius:7px;background:#1a1a2e;color:#eee;font-size:14px}
select:focus,input:focus{outline:none;border-color:#e94560}
button{width:100%;padding:12px;border:none;border-radius:7px;background:#e94560;color:#fff;font-size:15px;font-weight:bold;cursor:pointer;margin-top:20px}
button.s{background:#0f3460;margin-top:8px}
#status{margin-top:14px;text-align:center;font-size:13px;color:#a0a0b0}
</style></head><body>
<div class="c"><h2>S3 WiFi Setup</h2>
<label>WiFi Network</label>
<select id="ssid"><option value="">Tap Scan first...</option></select>
<button class="s" onclick="scan()">Scan Nearby Networks</button>
<label>Password</label>
<input type="password" id="pass" placeholder="Enter WiFi password">
<button onclick="connect()">Connect</button>
<div id="status"></div></div>
<script>
function $(id){return document.getElementById(id)}
function show(m){$('status').textContent=m}
async function scan(){show('Scanning...');try{let r=await fetch('/api/scan'),j=await r.json(),s=$('ssid');s.innerHTML='';j.aps.forEach(a=>{let o=document.createElement('option');o.value=a.ssid;o.textContent=a.ssid+'  ('+a.rssi+' dBm)';s.appendChild(o)});show(j.aps.length+' networks found')}catch(e){show('Scan failed')}}
async function connect(){let x=$('ssid').value,y=$('pass').value;if(!x){show('Select a network first');return}show('Connecting...');try{let r=await fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(x)+'&password='+encodeURIComponent(y)}),j=await r.json();show(j.status=='ok'?'Saved! Connecting...':'Error')}catch(e){show('Request failed')}}
</script></body></html>)raw";

static esp_err_t http_idx(httpd_req_t *r) {
    httpd_resp_set_hdr(r, "Cache-Control", "no-cache"); httpd_resp_set_type(r, "text/html");
    httpd_resp_send(r, HTML_PAGE, strlen(HTML_PAGE)); return ESP_OK;
}

static esp_err_t http_scan(httpd_req_t *r) {
    wifi_scan_config_t sc = { .show_hidden = true, .scan_type = WIFI_SCAN_TYPE_ACTIVE };
    esp_wifi_scan_stop(); esp_wifi_scan_start(&sc, false); vTaskDelay(pdMS_TO_TICKS(3000));
    uint16_t n = 0; esp_wifi_scan_get_ap_num(&n);
    wifi_ap_record_t *aps = calloc(n, sizeof(wifi_ap_record_t));
    esp_wifi_scan_get_ap_records(&n, aps);
    char j[4096]; int o = snprintf(j, sizeof(j), "{\"aps\":[");
    for (int i = 0; i < n && i < 20; i++)
        o += snprintf(j+o, sizeof(j)-o, "%s{\"ssid\":\"%s\",\"rssi\":%d}", i?",":"", aps[i].ssid, aps[i].rssi);
    o += snprintf(j+o, sizeof(j)-o, "]}"); free(aps);
    httpd_resp_set_type(r, "application/json"); httpd_resp_send(r, j, o); return ESP_OK;
}

static esp_err_t http_connect(httpd_req_t *r) {
    char b[512]; int l = httpd_req_recv(r, b, sizeof(b)-1);
    if (l <= 0) { httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Empty"); return ESP_FAIL; }
    b[l] = 0; memset(g_cfg_ssid, 0, 33); memset(g_cfg_pass, 0, 65);
    const char *p = strstr(b, "ssid="); if (!p) goto err;
    p += 5; const char *e = strchr(p, '&'); size_t n = e ? (size_t)(e-p) : strlen(p);
    if (n > 32) n = 32;
    snprintf(g_cfg_ssid, sizeof(g_cfg_ssid), "%.*s", (int)n, p);
    for (size_t i = 0; g_cfg_ssid[i]; i++) {
        if (g_cfg_ssid[i] == '+') g_cfg_ssid[i] = ' ';
        else if (g_cfg_ssid[i] == '%') { char h[3]={g_cfg_ssid[i+1],g_cfg_ssid[i+2],0}; g_cfg_ssid[i]=(char)strtol(h,NULL,16); memmove(g_cfg_ssid+i+1,g_cfg_ssid+i+3,strlen(g_cfg_ssid+i+3)+1); }
    }
    p = strstr(b, "password=");
    if (p) { p += 9; e = strchr(p, '&'); n = e ? (size_t)(e-p) : strlen(p); if (n > 63) n = 63;
        snprintf(g_cfg_pass, sizeof(g_cfg_pass), "%.*s", (int)n, p);
        for (size_t i = 0; g_cfg_pass[i]; i++) {
            if (g_cfg_pass[i] == '+') g_cfg_pass[i] = ' ';
            else if (g_cfg_pass[i] == '%') { char h[3]={g_cfg_pass[i+1],g_cfg_pass[i+2],0}; g_cfg_pass[i]=(char)strtol(h,NULL,16); memmove(g_cfg_pass+i+1,g_cfg_pass+i+3,strlen(g_cfg_pass+i+3)+1); }
        }
    }
    if (!g_cfg_ssid[0]) goto err;
    ESP_LOGI(TAG, "Web: SSID=%s", g_cfg_ssid);
    nvs_save(g_cfg_ssid, g_cfg_pass);
    httpd_resp_set_type(r, "application/json"); httpd_resp_sendstr(r, "{\"status\":\"ok\"}");
    /* Apply immediately */
    wifi_config_t wc = { .sta = { .threshold.authmode = WIFI_AUTH_WPA2_PSK } };
    snprintf((char*)wc.sta.ssid, sizeof(wc.sta.ssid), "%s", g_cfg_ssid);
    snprintf((char*)wc.sta.password, sizeof(wc.sta.password), "%s", g_cfg_pass);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_connect();
    return ESP_OK;
err: httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Bad request"); return ESP_FAIL;
}

static int cmd_ap(int argc, char **argv)
{
    ESP_LOGW(TAG, "AP mode will interrupt CSI capture (single radio channel conflict)!");
    esp_netif_create_default_wifi_ap();
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    wifi_config_t ac = {0};
    snprintf((char*)ac.ap.ssid, sizeof(ac.ap.ssid), "%s", AP_SSID);
    ac.ap.ssid_len = strlen(AP_SSID); ac.ap.max_connection = 3;
    ac.ap.authmode = WIFI_AUTH_OPEN; ac.ap.channel = AP_CHANNEL;
    esp_wifi_set_config(WIFI_IF_AP, &ac); esp_wifi_start();
    printf("\nAP: %s  ->  http://192.168.4.1\n", AP_SSID);

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG(); hc.lru_purge_enable = true; hc.stack_size = 8192;
    httpd_start(&g_httpd, &hc);
    httpd_uri_t u1 = {.uri="/", .method=HTTP_GET, .handler=http_idx};
    httpd_uri_t u2 = {.uri="/api/scan", .method=HTTP_GET, .handler=http_scan};
    httpd_uri_t u3 = {.uri="/api/connect", .method=HTTP_POST, .handler=http_connect};
    httpd_register_uri_handler(g_httpd, &u1);
    httpd_register_uri_handler(g_httpd, &u2);
    httpd_register_uri_handler(g_httpd, &u3);
    return 0;
}

/* ══════════════════════════════════════════════
 *  MAIN
 * ══════════════════════════════════════════════ */
void app_main(void)
{
    /* NVS */
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND)
    { nvs_flash_erase(); nvs_flash_init(); }

    /* UART1 bridge to P4 */
    uart_init();
    ESP_LOGI(TAG, "UART1 ready: TX=%d RX=%d baud=%d", TXD_PIN, RXD_PIN, UART_BAUD);
    xTaskCreate(uart_frame_demux_task, "uart_demux", 8192, NULL, 5, NULL);

    esp_log_level_set("esp_radar", ESP_LOG_INFO);
    esp_log_level_set("csi_detection_task", ESP_LOG_ERROR); /* suppress high-rate warnings */

    /* Console REPL */
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_uart_config_t uc = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    rc.prompt = "s3> ";
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uc, &rc, &repl));

    /* ── esp_radar init (+ LLTF-only for router compatibility) ─── */
    esp_radar_csi_config_t csi_cfg = ESP_RADAR_CSI_CONFIG_DEFAULT();
    esp_radar_wifi_config_t wifi_cfg = ESP_RADAR_WIFI_CONFIG_DEFAULT();
    esp_radar_dec_config_t dec_cfg = ESP_RADAR_DEC_CONFIG_DEFAULT();
    memcpy(csi_cfg.filter_mac, "\x1a\x00\x00\x00\x00\x00", 6);
    csi_cfg.csi_recv_interval = g_send_data_interval;
    csi_cfg.htltf_en = false;          /* LLTF-only: better router compatibility */
    csi_cfg.stbc_htltf2_en = false;
    dec_cfg.wifi_radar_cb = wifi_radar_cb;
    dec_cfg.outliers_threshold = 0;

    ESP_ERROR_CHECK(esp_radar_wifi_init(&wifi_cfg));

    /* Enable MGMT+DATA promiscuous capture for max CSI yield (no display → no SPI conflict) */
    {
        wifi_promiscuous_filter_t pf = {
            .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
        };
        esp_wifi_set_promiscuous_filter(&pf);
        ESP_LOGI(TAG, "Promiscuous filter: MGMT+DATA");
    }

    ESP_ERROR_CHECK(esp_radar_csi_init(&csi_cfg));
    ESP_ERROR_CHECK(esp_radar_dec_init(&dec_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_event_handler, NULL));

    /* Register ALL commands (reference + our additions) */
    cmd_register_ping();
    cmd_register_wifi_config();
    cmd_register_wifi_scan();
    cmd_register_radar();
    {
        /* Our SoftAP HTTP portal */
        const esp_console_cmd_t c = { .command = "ap", .help = "Start SoftAP HTTP web portal", .func = &cmd_ap };
        ESP_ERROR_CHECK(esp_console_cmd_register(&c));
    }
    {
        /* version / restart / reset / log from system_cmd */
        esp_console_cmd_t cmds[] = {
            {.command="version", .help="Get version of chip and SDK", .func=&cmd_version},
            {.command="restart", .help="Software reset of the chip", .func=&cmd_restart},
            {.command="reset",   .help="Clear NVS configuration", .func=&cmd_reset},
        };
        for (int i = 0; i < sizeof(cmds)/sizeof(cmds[0]); i++)
            ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
        log_args.tag = arg_str0("t", "tag", "<tag>", "Tag of the log entries to enable");
        log_args.level = arg_str0("l", "level", "<level>", "Selects log level (NONE,ERR,WARN,INFO,DEBUG,VER)");
        log_args.end = arg_end(2);
        const esp_console_cmd_t lc = { .command = "log", .help = "Set log level for given tag", .func = &cmd_log, .argtable = &log_args };
        ESP_ERROR_CHECK(esp_console_cmd_register(&lc));
    }

    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    /* CSI queue + print task (create BEFORE radar start to avoid dropping events) */
    g_csi_queue = xQueueCreate(64, sizeof(void *));
    xTaskCreate(csi_data_print_task, "csi_print", 6144, NULL, 2, NULL);

    /* Auto-connect from NVS BEFORE radar start */
    char ssid[33] = {0}, pass[65] = {0};
    if (nvs_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "Auto-connecting to %s", ssid);
        wifi_config_t wc = { .sta = { .threshold.authmode = WIFI_AUTH_WPA2_PSK } };
        snprintf((char*)wc.sta.ssid, sizeof(wc.sta.ssid), "%s", ssid);
        snprintf((char*)wc.sta.password, sizeof(wc.sta.password), "%s", pass);
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &wc);
        esp_wifi_connect();
    } else {
        ESP_LOGI(TAG, "No saved WiFi. Use 'wifi_config -s <ssid> -p <pass>' or 'ap' for web portal.");
    }

    /* Load saved radar config from NVS */
    nvs_load_rcfg();

    /* Start radar (WiFi should be connected by now) */
    esp_radar_start();

    /* Start HTTP event reporter (async, no block on send failure) */
    event_reporter_init();
}
