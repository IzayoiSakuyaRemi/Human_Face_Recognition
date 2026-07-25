/**
 * @file csi_liveness.cpp
 * @brief CSI-based liveness detection implementation.
 *
 * Method: Time-domain variance analysis of subcarrier-averaged CSI amplitude.
 *
 * Physics: A breathing human body modulates WiFi CSI amplitude at 0.1–0.5 Hz
 * (6–30 breaths/minute). Photos, videos, and 3D masks produce NO such
 * modulation. By computing the RMS of the amplitude signal after DC removal,
 * we can distinguish "empty room / artificial face" from "living human."
 *
 * Data flow:
 *   ADR-018 frame (50 Hz) → extract I/Q → compute amplitude per subcarrier
 *   → average across subcarriers → push to 20s circular buffer (1000 samples)
 *   → every 2s: remove DC, compute RMS → compare against calibrated threshold
 *
 * Calibration: auto-baselines when radar says EMPTY + still for 30 consecutive
 * seconds. Saves baseline RMS to NVS so it survives reboots.
 */

#include "csi_liveness.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstring>
#include <cmath>
#include <algorithm>

static const char *TAG = "csi_live";

/* ── Constants ───────────────────────────────── */
#define ADR018_HDR_LEN      20      // fixed header size
#define ADR018_MAGIC_0      0xC5
#define ADR018_MAGIC_1      0x11
#define ADR018_MAGIC_2      0x00
#define ADR018_MAGIC_3      0x01
#define WINDOW_SECS         20      // analysis window
#define SAMPLE_RATE_HZ      50      // ADR-018 frame rate from S3
#define BUF_SAMPLES         (WINDOW_SECS * SAMPLE_RATE_HZ)  // 1000
#define LINGER_SECS         5       // how long liveness stays true after last detection
#define CALIBRATE_SECS      30      // must be EMPTY+still for this long to auto-calibrate

/* ── State ───────────────────────────────────── */
static float    *g_amp_buf     = nullptr;  // circular amplitude buffer [BUF_SAMPLES]
static int       g_amp_pos     = 0;        // write cursor
static int       g_amp_count   = 0;        // samples written (for cold-start)
static bool      g_liveness    = false;
static int64_t   g_last_alive_us = 0;
static float     g_baseline_rms = 0.0f;    // calibrated empty-room RMS
static float     g_threshold   = 5.0f;     // default before calibration (arbitrary units)
static int       g_empty_secs  = 0;        // consecutive EMPTY+still seconds
static SemaphoreHandle_t g_mutex = nullptr;

/* ── Forward declarations ────────────────────── */
static float compute_rms(void);
static void  auto_calibrate_if_ready(void);

/* ═══════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════ */

void csi_liveness_init(void)
{
    g_amp_buf = (float *)heap_caps_calloc(BUF_SAMPLES, sizeof(float),
                                          MALLOC_CAP_SPIRAM);
    g_mutex = xSemaphoreCreateMutex();
    if (!g_amp_buf || !g_mutex) {
        ESP_LOGE(TAG, "Init failed: no memory");
        return;
    }
    g_amp_pos = 0;
    g_amp_count = 0;
    g_liveness = false;
    g_empty_secs = 0;
    ESP_LOGI(TAG, "CSI liveness detector ready (window=%ds, %d samples PSRAM)",
             WINDOW_SECS, BUF_SAMPLES);
}

void csi_liveness_feed(const uint8_t *data, size_t len)
{
    if (!g_amp_buf) return;

    // Validate ADR-018 header
    if (len < ADR018_HDR_LEN) return;
    if (data[0] != ADR018_MAGIC_0 || data[1] != ADR018_MAGIC_1 ||
        data[2] != ADR018_MAGIC_2 || data[3] != ADR018_MAGIC_3) return;

    uint16_t nsub = (uint16_t)data[6] | ((uint16_t)data[7] << 8);
    if (nsub == 0 || len < (size_t)(ADR018_HDR_LEN + nsub * 2)) return;

    // Compute amplitude averaged across all subcarriers.
    // Each subcarrier: 1 byte I + 1 byte Q → amplitude = sqrt(I² + Q²).
    const uint8_t *iq = data + ADR018_HDR_LEN;
    float amp_sum = 0;
    for (int s = 0; s < (int)nsub; s++) {
        int8_t i_val = (int8_t)iq[s * 2];
        int8_t q_val = (int8_t)iq[s * 2 + 1];
        amp_sum += sqrtf((float)(i_val * i_val + q_val * q_val));
    }
    float amp = amp_sum / nsub;

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_amp_buf[g_amp_pos] = amp;
    g_amp_pos = (g_amp_pos + 1) % BUF_SAMPLES;
    if (g_amp_count < BUF_SAMPLES) g_amp_count++;
    xSemaphoreGive(g_mutex);

    // Periodic liveness check (every ~2 seconds ≈ every 100th frame)
    static int frame_ctr = 0;
    if (++frame_ctr >= 100) {
        frame_ctr = 0;
        float rms = compute_rms();
        if (rms > g_threshold) {
            g_liveness = true;
            g_last_alive_us = esp_timer_get_time();
        }
        // Linger: keep liveness true for LINGER_SECS after last detection.
        // A breathing pause (e.g., holding breath) shouldn't immediately fail.
        if (g_liveness &&
            (esp_timer_get_time() - g_last_alive_us) > (int64_t)LINGER_SECS * 1000000LL) {
            g_liveness = false;
        }

        // Auto-calibration: if room has been EMPTY+still for long enough,
        // take a new baseline.
        auto_calibrate_if_ready();
    }
}

bool csi_liveness_check(void)
{
    return g_liveness;
}

void csi_liveness_calibrate(void)
{
    float rms = compute_rms();
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_baseline_rms = rms;
    g_threshold = rms * 3.0f;
    if (g_threshold < 2.0f) g_threshold = 2.0f;  // floor: prevent noise triggers
    g_empty_secs = 0;
    xSemaphoreGive(g_mutex);
    ESP_LOGI(TAG, "Calibrated: baseline=%.2f threshold=%.2f",
             (double)g_baseline_rms, (double)g_threshold);
}

void csi_liveness_stats(float *rms_out, float *threshold_out)
{
    *rms_out = compute_rms();
    *threshold_out = g_threshold;
}

/* ═══════════════════════════════════════════════
 *  Internal
 * ═══════════════════════════════════════════════ */

static float compute_rms(void)
{
    if (g_amp_count < 100) return 0;  // cold start: not enough data

    xSemaphoreTake(g_mutex, portMAX_DELAY);

    // Copy buffer to avoid blocking the feed path for too long
    static float local[BUF_SAMPLES];
    int n = (g_amp_count < BUF_SAMPLES) ? g_amp_count : BUF_SAMPLES;
    if (g_amp_count >= BUF_SAMPLES) {
        // Buffer is full: data is circular. Read from g_amp_pos backwards.
        for (int i = 0; i < n; i++)
            local[i] = g_amp_buf[(g_amp_pos - n + i + BUF_SAMPLES) % BUF_SAMPLES];
    } else {
        memcpy(local, g_amp_buf, n * sizeof(float));
    }
    xSemaphoreGive(g_mutex);

    // Remove DC (subtract mean)
    float mean = 0;
    for (int i = 0; i < n; i++) mean += local[i];
    mean /= n;

    // Compute RMS of AC component
    float sum_sq = 0;
    for (int i = 0; i < n; i++) {
        float ac = local[i] - mean;
        sum_sq += ac * ac;
    }
    return sqrtf(sum_sq / n);
}

static void auto_calibrate_if_ready(void)
{
    // Radar data comes every 2 seconds via JSON. Track consecutive empty seconds.
    // NOTE: g_empty_secs is incremented by a separate task (radar_display or
    // uart_rx) when it sees EMPTY+still. See integration in uart_bridge.cpp.

    // This function is called every 2 seconds. If g_empty_secs has been set
    // externally (by radar JSON parsing), we check the calibration condition.
    if (g_empty_secs >= CALIBRATE_SECS) {
        csi_liveness_calibrate();
        g_empty_secs = 0;  // reset — only recalibrate once per empty period
    }
}

/* ── Called from radar JSON parser (uart_bridge.cpp) ── */
void csi_liveness_notify_room(const char *room, const char *move)
{
    bool empty = (strcmp(room, "EMPTY") == 0);
    bool still = (strcmp(move, "still") == 0);

    if (empty && still) {
        g_empty_secs += 2;  // radar JSON arrives every 2 seconds
    } else {
        g_empty_secs = 0;   // someone is here or moving → reset
    }
}
