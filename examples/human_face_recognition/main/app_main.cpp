#include "frame_cap_pipeline.hpp"
#include "who_recognition_app_lcd.hpp"
#include "who_recognition_app_term.hpp"
#include "who_spiflash_fatfs.hpp"
#include "event_reporter.hpp"
#include "xiaozhi/uart_frame_protocol.h"
#include <cstdlib>
#include <cstring>
#include "uart_bridge.hpp"
#include "radar_display.hpp"
// #include "wifi_provisioning.hpp"  // C5 removed
#include "driver/gpio.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
// #include "esp_wifi.h"     // C5 removed
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_vfs_fat.h"
extern "C" { esp_err_t bsp_eth_init(void); }
#include "driver/sdspi_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_brookesia.hpp"
#include "who_lvgl_lcd.hpp"
#include "face_recognition_app.hpp"
#include "settings_app.hpp"
#include "xiaozhi_app.hpp"
#include "csi_liveness.hpp"
#include "speaker_verification.hpp"
#include "dl_feat_verification_database.hpp"
// wallpaper now loaded from SD card or NVS only (no compiled-in default)
#include <cstdio>
#include <cstring>
#include <cmath>

extern char g_last_recog_face[64];
static const char *TAG = "app_main";
who::app::WhoRecognitionAppLCD *g_recognition_app = nullptr;
bool g_voice_paused = true;  // paused until Camera App opens

// Brookesia globals
ESP_Brookesia_Phone *g_phone = nullptr;
volatile int g_wifi_icon_state = 0;  // updated by uart_rx_task, applied by clock timer
// Dynamic wallpaper tracking (for cleanup on switch)
// Non-static so SettingsApp can free boot-time allocation
lv_image_dsc_t *g_active_wp_dsc = nullptr;
void *g_active_wp_data = nullptr;

using namespace who::frame_cap;
using namespace who::app;

EventGroupHandle_t g_recog_event_group = nullptr;
esp_codec_dev_handle_t g_mic_handle = nullptr;
esp_codec_dev_handle_t g_speaker_handle = nullptr;  // for xiaozhi audio bridge
static SemaphoreHandle_t g_espdl_mutex = nullptr;
static volatile int g_skip_detect_count = 0;
portMUX_TYPE g_face_spinlock = portMUX_INITIALIZER_UNLOCKED;

// Thread-safe read of g_last_recog_face (cross-core: written on Core1, read on Core0)
static void face_result_copy(char *dst, size_t sz) {
    portENTER_CRITICAL(&g_face_spinlock);
    size_t n = strnlen(g_last_recog_face, sizeof(g_last_recog_face));
    if (n >= sz) n = sz - 1;
    memcpy(dst, g_last_recog_face, n);
    dst[n] = '\0';
    portEXIT_CRITICAL(&g_face_spinlock);
}

// Speaker verification — multi-user with rolling audio buffer
#define MAX_VOICE_USERS 10
#define ROLLING_BUF_SECS      8
#define ROLLING_BUF_SAMPLES   (16000 * ROLLING_BUF_SECS)  // 128000
static SpeakerVerification *g_speaker_verifier = nullptr;
static dl::feat::FeatVerificationDatabase *g_voice_db = nullptr;
bool g_voice_enrolling = false;
bool g_voice_enroll_allowed = true;  // Settings toggle removed — always allowed
bool g_voice_verifying = false;
static int g_voice_collect_samples = 0;
static int16_t *g_voice_cap_buf = nullptr;   // captured audio for verification

/* ── Remote voice verification (P4→S3→WiFi→3090) ── */
bool g_voice_remote_mode = true;  // remote voice verification server
static SemaphoreHandle_t g_remote_score_sem = nullptr;
static float g_remote_voice_score = 0.0f;
static bool g_remote_voice_ok = false;

/** UART frame callback: S3 sends CTRL_VOICE_SCORE with JSON {"score":0.85,"who":"user"} */
static void on_voice_score_cb(uint8_t type, const uint8_t *data, size_t len)
{
    if (type != UART_FRAME_CTRL || len < CTRL_FRAME_HEADER_SIZE) return;
    if (data[1] != CTRL_VOICE_SCORE) return;
    const char *json = (const char *)&data[CTRL_FRAME_HEADER_SIZE];
    const char *sp = strstr(json, "\"score\":");
    if (sp) g_remote_voice_score = strtof(sp + 8, nullptr);
    // Parse who field for dual-centroid result
    bool who_is_user = false;
    sp = strstr(json, "\"who\":");
    if (sp) {
        sp = strchr(sp, '"'); if (sp) sp = strchr(sp + 1, '"');
        if (sp && strncmp(sp + 1, "user", 4) == 0) who_is_user = true;
    }
    g_remote_voice_ok = who_is_user;  // true only if dual-centroid says "user"
    if (g_remote_score_sem) xSemaphoreGive(g_remote_score_sem);
    ESP_LOGI(TAG, "Voice score: %.4f who=%s", (double)g_remote_voice_score, who_is_user ? "user" : "other");
}
#define MAX_ENROLL_PER_USER 5
static float *g_voice_embeddings[MAX_VOICE_USERS][MAX_ENROLL_PER_USER] = {};
static int g_voice_enroll_count[MAX_VOICE_USERS] = {};
static int g_voice_user_count = 0;
static int g_embedding_dim = 0;
// Rolling circular buffer: always keeps last 5s of audio
static int16_t *g_rolling_buf = nullptr;
static int g_rolling_wr = 0;       // write position (circular)
static int g_rolling_total = 0;    // total samples written (for cold-start check)
static int g_post_detect_samples = 0;
int g_pending_cmd_id = 0;
static bool g_face_triggered = false;  // face detection was requested for pending cmd

// Voiceprint match threshold (cosine similarity) — run-time adjustable
// from Settings. Default 0.35: genuine scores 0.5-0.7, impostors <0.2.
float g_voice_threshold = 0.35f;
static bool g_voice_threshold_calibrated = false;

// Geometric mean fusion: sqrt(face × voice). Naturally penalizes
// single-modality failure — if either score drops to zero (face=who?
// or voice=NO_MATCH), auth_score = 0 regardless of the other modality.
// User worst: sqrt(0.52×0.37)=0.44; best: sqrt(0.76×0.57)=0.66.
// Face-strong/voice-fail: sqrt(0.71×0.09)=0.25 → correctly blocked.
// Threshold 0.35 provides comfortable margin.
#define FUSION_THRESHOLD 0.35f

// Reset voice DB from Settings UI. Exists because MultiNet reliably fails to
// recognize voice command 8 ("qing chu sheng wen") — the phrase is a model
// blind spot. Returns the number of cleared enrollments.
int voice_db_reset(void)
{
    int cleared = g_voice_user_count;
    // Serialize against in-flight speaker verification (which runs under this
    // mutex) to avoid freeing embeddings mid-compare.
    if (g_espdl_mutex) xSemaphoreTake(g_espdl_mutex, portMAX_DELAY);
    if (g_voice_db) g_voice_db->clear();
    for (int i = 0; i < MAX_VOICE_USERS; i++) {
        for (int e = 0; e < MAX_ENROLL_PER_USER; e++) {
            free(g_voice_embeddings[i][e]);
            g_voice_embeddings[i][e] = nullptr;
        }
        g_voice_enroll_count[i] = 0;
    }
    g_voice_user_count = 0;
    g_voice_threshold = 0.35f;          // reset to default floor
    g_voice_threshold_calibrated = false;
    if (g_espdl_mutex) xSemaphoreGive(g_espdl_mutex);
    ESP_LOGI(TAG, "Voice DB reset from Settings UI (%d cleared)", cleared);
    return cleared;
}

// ── Adaptive threshold calibration ──
// After 3+ enrollments, compute cross-enrollment self-similarity to calibrate
// the voice threshold.  Consistent voices get a higher bar (harder for
// imposters to pass); variable voices get a lower floor so the user isn't
// locked out.  Called after each enrollment and on boot.
// Formula: thr = max(0.30, self_mean × 0.50)
//   0.50× accounts for the 20-30% session variability drop between enrollment
//   and verification. Self-similarity is typically 0.70-0.90 (same session),
//   while cross-session genuine scores are typically 0.50-0.70.
//   – self_mean=0.80 → thr=0.40  (typical, blocks imposters ~0.2-0.3)
//   – self_mean=0.70 → thr=0.35  (variable user, floor blocks most imposters)
static void voice_calibrate_threshold(void)
{
    int user = (g_voice_user_count > 0) ? (g_voice_user_count - 1) : 0;
    int n = g_voice_enroll_count[user];
    if (n < 3) {
        ESP_LOGI(TAG, "Thr calib: need 3+ enrollments (have %d), keeping %.3f",
                 n, (double)g_voice_threshold);
        return;
    }

    // Compute all pairwise cosine similarities between this user's enrollments
    float sum = 0, min_s = 1.0f, max_s = 0.0f;
    int pairs = 0;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (!g_voice_embeddings[user][i] || !g_voice_embeddings[user][j]) continue;
            float s = g_speaker_verifier->compute_similarity(
                g_voice_embeddings[user][i], g_voice_embeddings[user][j]);
            sum += s; if (s < min_s) min_s = s; if (s > max_s) max_s = s;
            pairs++;
        }
    }
    if (pairs < 2) return;

    float self_mean = sum / (float)pairs;
    float calibrated = fmaxf(0.30f, self_mean * 0.50f);
    g_voice_threshold = calibrated;
    g_voice_threshold_calibrated = true;

    ESP_LOGI(TAG, "Thr calib: user=%d enrollers=%d pairs=%d self=%.3f(%.3f-%.3f) → thr=%.3f",
             user, n, pairs, (double)self_mean, (double)min_s, (double)max_s,
             (double)calibrated);
}

// Simple VAD + AGC state for voice preprocessing
struct VoiceAudioState {
    float noise_floor = 0.0f;       // running average of quiet energy
    float speech_energy = 0.0f;     // running average of speech energy
    int silence_frames = 0;         // consecutive silence frames
    int speech_frames = 0;          // consecutive speech frames
    bool is_speaking = false;
};
static VoiceAudioState g_voice_audio;

// Apply simple AGC: normalize each frame to a target RMS level
static void apply_agc(int16_t *buf, int len, float target_rms = 2000.0f) {
    float sum = 0;
    for (int i = 0; i < len; i++) sum += (float)buf[i] * buf[i];
    float rms = sqrtf(sum / len);
    if (rms < 10.0f) return;  // too quiet, don't amplify noise
    float gain = target_rms / rms;
    if (gain > 5.0f) gain = 5.0f;   // limit max gain to avoid amplifying noise
    if (gain < 0.5f) gain = 0.5f;   // limit min gain
    for (int i = 0; i < len; i++) {
        float s = buf[i] * gain;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        buf[i] = (int16_t)s;
    }
}

struct voice_task_params_t {
    esp_mn_iface_t *multinet;
    model_iface_data_t *mn_data;
    int chunksize;
};

static void voice_recognition_task(void *arg)
{
    voice_task_params_t *p = (voice_task_params_t *)arg;
    esp_mn_iface_t *multinet = p->multinet;
    model_iface_data_t *mn_data = p->mn_data;
    int chunksize = p->chunksize;
    free(p);

    // Mono read: codec honors channel_mask=1 and delivers mono samples.
    // (The earlier ×2 "stereo safety" sizing was a mis-fix: each read consumed
    // 1024 samples but only the first 512 were processed — HALF the audio was
    // silently discarded, gutting MultiNet recognition. Log proof: 16 reads/sec
    // × 1024 = 16kHz, i.e. mono stream fully consumed at double stride.)
    int read_bytes = chunksize * sizeof(int16_t);
    int16_t *audio_buf = (int16_t *)malloc(read_bytes);
    if (!audio_buf) {
        ESP_LOGE(TAG, "Voice: buffer alloc failed");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Voice ready: chunksize=%d mono samples, listening...", chunksize);

    // Memory report
    ESP_LOGI(TAG, "===== Memory Report =====");
    ESP_LOGI(TAG, "Internal DRAM: free=%lu KB / largest=%lu KB",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
    ESP_LOGI(TAG, "PSRAM:        free=%lu KB / largest=%lu KB",
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024,
             heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024);
    ESP_LOGI(TAG, "Total free:   %lu KB",
             heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024);
    ESP_LOGI(TAG, "=========================");

    int64_t last_log = 0;
    int read_count = 0;
    bool was_paused = false;
    ESP_LOGI(TAG, "🎙 Voice task running: read_bytes=%d stack_hwm=%lu",
             (int)read_bytes, uxTaskGetStackHighWaterMark(NULL));

    while (g_mic_handle) {
        if (g_voice_paused) {
            if (!was_paused) {
                ESP_LOGI(TAG, "🎙 Voice task PAUSED (xiaozhi mode active)");
                was_paused = true;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (was_paused) {
            ESP_LOGI(TAG, "Voice task RESUMED, hwm=%lu",
                     uxTaskGetStackHighWaterMark(NULL));
            was_paused = false;
            // Reset MultiNet and all detection state on resume
            if (multinet && mn_data) multinet->clean(mn_data);
            g_skip_detect_count = 0;
            g_post_detect_samples = 0;
            g_voice_enrolling = false;
            g_voice_verifying = false;
            g_pending_cmd_id = 0;
            g_face_triggered = false;
        }

        // esp_codec_dev_read returns ESP_CODEC_DEV_OK (0) on success,
        // NOT the number of bytes read! The actual data is in audio_buf.
        int ret = esp_codec_dev_read(g_mic_handle, audio_buf, read_bytes);
        read_count++;
        int64_t now = esp_timer_get_time();

        if (ret != 0) {
            ESP_LOGW(TAG, "Codec read error: %d, retrying...", ret);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (now - last_log > 1000000) {
            int nz = 0;
            for (int i = 0; i < chunksize; i++) if (audio_buf[i] != 0) nz++;
            ESP_LOGD(TAG, "Voice: %d reads/sec, nonzero=%d/%d stack_free=%lu",
                     read_count, nz, chunksize, uxTaskGetStackHighWaterMark(NULL));
            read_count = 0;
            last_log = now;
        }

        // AGC disabled for MultiNet — per-frame gain destroys amplitude envelope
        // that the model expects. Speaker verification has its own normalization.
        // apply_agc(audio_buf, chunksize, 2000.0f);

        // ---- Always append to rolling circular buffer ----
        if (g_rolling_buf) {
            int space = ROLLING_BUF_SAMPLES - g_rolling_wr;
            int first = (chunksize < space) ? chunksize : space;
            memcpy(g_rolling_buf + g_rolling_wr, audio_buf, first * sizeof(int16_t));
            if (first < chunksize)
                memcpy(g_rolling_buf, audio_buf + first, (chunksize - first) * sizeof(int16_t));
            g_rolling_wr = (g_rolling_wr + chunksize) % ROLLING_BUF_SAMPLES;
            g_rolling_total += chunksize;
        }

        // ---- Speaker verification: post-detect delay then snapshot ring buffer ----
        if (g_post_detect_samples > 0) {
            g_post_detect_samples -= chunksize;
            if (g_post_detect_samples <= 0) {
                // Snapshot last 3s from rolling buffer (clean voice, no command phrase)
                int available = (g_rolling_total >= ROLLING_BUF_SAMPLES) ? ROLLING_BUF_SAMPLES : g_rolling_total;
                int max_samples = 16000 * 3;  // 3s capture window
                int to_copy = (available < max_samples) ? available : max_samples;
                int start = (g_rolling_wr - to_copy + ROLLING_BUF_SAMPLES) % ROLLING_BUF_SAMPLES;
                for (int i = 0; i < to_copy; i++)
                    g_voice_cap_buf[i] = g_rolling_buf[(start + i) % ROLLING_BUF_SAMPLES];
                g_voice_collect_samples = to_copy;
                ESP_LOGI(TAG, "Voice: captured %d samples from rolling buffer", to_copy);
                // Reset ring buffer to prevent old commands from leaking into next capture
                g_rolling_total = 0;
                g_rolling_wr = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        // Process captured audio
        if ((g_voice_enrolling || g_voice_verifying) && g_voice_collect_samples > 0) {
            int total = g_voice_collect_samples;
            g_voice_collect_samples = 0;
            bool was_enrolling = g_voice_enrolling;
            g_voice_enrolling = false;
            g_voice_verifying = false;

            // ── REMOTE MODE: send PCM_UP → S3 → WiFi → voice server ──
            if (g_voice_remote_mode) {
                const int frame_samples = 960;
                // Pad short audio with silence to full frames
                int padded = (total + frame_samples - 1) / frame_samples * frame_samples;
                if (padded > 50 * frame_samples) padded = 50 * frame_samples;
                for (int i = total; i < padded; i++) g_voice_cap_buf[i] = 0;
                int total_frames = padded / frame_samples;
                int64_t t0 = esp_timer_get_time();
                for (int f = 0; f < total_frames; f++) {
                    uint8_t fbuf[PCM_FRAME_MAX_TOTAL];
                    size_t flen = uart_frame_build_pcm(fbuf, sizeof(fbuf),
                        UART_FRAME_PCM_UP,
                        (uint8_t)(f == 0 ? PCM_FLAG_FIRST : 0),
                        (uint16_t)f,
                        g_voice_cap_buf + f * frame_samples,
                        (uint16_t)frame_samples);
                    if (flen == 0) continue;
                    // Slow-paced send: 50ms between frames = 20 fps.
                    // UART drains at 21ms/frame, double margin prevents queue fill.
                    uart_bridge_send_frame(UART_FRAME_PCM_UP, fbuf, flen);
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                ESP_LOGI(TAG, "Remote: %d PCM_UP frames sent in %lld us",
                         total_frames, esp_timer_get_time() - t0);

                // Wait for voice score (10s timeout)
                int pending = g_pending_cmd_id;
                g_pending_cmd_id = 0;
                bool voice_ok = false, face_ok = false;
                float voice_score = 0.0f;
                if (xSemaphoreTake(g_remote_score_sem, pdMS_TO_TICKS(10000)) == pdTRUE) {
                    voice_score = g_remote_voice_score;
                    voice_ok = (voice_score > 0.75f);
                    ESP_LOGI(TAG, "Voice score=%.4f %s", (double)voice_score, voice_ok ? "MATCH" : "NO");
                } else {
                    ESP_LOGW(TAG, "Voice timeout");
                }
                // Wait for face result (if face recognition was triggered)
                if (g_face_triggered) {
                    TickType_t face_start = xTaskGetTickCount();
                    while (g_face_triggered && xTaskGetTickCount() - face_start < pdMS_TO_TICKS(3000)) {
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                    face_ok = (g_last_recog_face[0] != '\0' && strcmp(g_last_recog_face, "who?") != 0);
                    ESP_LOGI(TAG, "Face result: %s (%s)", g_last_recog_face, face_ok ? "OK" : "NONE");
                }
                // Display combined result
                bool auth_ok = voice_ok && face_ok;
                if (g_recognition_app) {
                    g_recognition_app->set_exec_text(auth_ok ? "ALLOW" : "ALARM");
                    g_recognition_app->set_status_text(auth_ok ? "Voice+Face OK" : "AUTH FAILED");
                }
                // Report event via S3 → event stream site
                {
                    const char *cmd_names[] = {"", "Open TV", "Close TV", "Open Door", "Close Door", "Identify"};
                    const char *cmd_name = (pending >= 1 && pending <= 5) ? cmd_names[pending] : "Unknown";
                    char jb[128];
                    snprintf(jb, sizeof(jb),
                             "\"cmd\":\"%s\",\"voice\":\"%s\",\"face\":\"%s\",\"result\":\"%s\"",
                             cmd_name,
                             voice_ok ? "PASS" : "FAIL",
                             face_ok ? "PASS" : "-",
                             auth_ok ? "ALLOW" : "DENY");
                    report_event("voice_command", jb);
                }
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }

            // ── Enrollment quality gate ──
            // Reject poor-quality audio BEFORE running the model. A single bad
            // enrollment pollutes the user's identity permanently.
            // RMS < 500  → too quiet (whisper, mic too far)
            // SNR < 10dB → too noisy (wind, background chatter)
            if (was_enrolling) {
                float rms = 0;
                for (int i = 0; i < total; i++) {
                    float s = (float)g_voice_cap_buf[i];
                    rms += s * s;
                }
                rms = sqrtf(rms / (float)total);
                float noise_rms = sqrtf(g_voice_audio.noise_floor);
                float snr = 20.0f * log10f(rms / (noise_rms + 1e-10f));

                if (rms < 500.0f) {
                    ESP_LOGW(TAG, "Enroll rejected: too quiet (RMS=%.0f, %d samples)",
                             (double)rms, total);
                    if (g_recognition_app) g_recognition_app->set_exec_text("Too quiet, speak louder");
                    vTaskDelay(pdMS_TO_TICKS(1));
                    continue;
                }
                if (snr < 10.0f) {
                    ESP_LOGW(TAG, "Enroll rejected: too noisy (RMS=%.0f noise=%.0f SNR=%.1fdB)",
                             (double)rms, (double)noise_rms, (double)snr);
                    if (g_recognition_app) g_recognition_app->set_exec_text("Too noisy, retry");
                    vTaskDelay(pdMS_TO_TICKS(1));
                    continue;
                }
                ESP_LOGI(TAG, "Enroll quality OK: RMS=%.0f SNR=%.1fdB %d samples",
                         (double)rms, (double)snr, total);
            }

            if (xSemaphoreTake(g_espdl_mutex, pdMS_TO_TICKS(100))) {
                // DEBUG: Save captured audio to SD card for Fbank comparison
                {
                    char wav_path[64];
                    snprintf(wav_path, sizeof(wav_path), "/sdcard/debug_%s_%d.wav",
                             was_enrolling ? "enroll" : "verify", (int)esp_timer_get_time());
                    FILE *fw = fopen(wav_path, "wb");
                    if (fw) {
                        int data_bytes = total * sizeof(int16_t);
                        int file_size = 44 + data_bytes;
                        uint8_t hdr[44] = {0};
                        memcpy(hdr, "RIFF", 4); hdr[4]=file_size-8; hdr[5]=(file_size-8)>>8; hdr[6]=(file_size-8)>>16; hdr[7]=(file_size-8)>>24;
                        memcpy(hdr+8, "WAVEfmt ", 8); hdr[16]=16; hdr[20]=1; hdr[22]=1; hdr[24]=0x80; hdr[25]=0x3E; hdr[28]=0x00; hdr[29]=0x7D; hdr[32]=2; hdr[34]=16;
                        memcpy(hdr+36,"data",4); hdr[40]=data_bytes; hdr[41]=data_bytes>>8; hdr[42]=data_bytes>>16; hdr[43]=data_bytes>>24;
                        fwrite(hdr,1,44,fw); fwrite(g_voice_cap_buf,1,data_bytes,fw); fclose(fw);
                        ESP_LOGI(TAG, "DEBUG: Saved %s (%d samples)", wav_path, total);
                    }
                }
                float *emb = g_speaker_verifier->run(g_voice_cap_buf, total);
                xSemaphoreGive(g_espdl_mutex);
                vTaskDelay(pdMS_TO_TICKS(5));  // yield to LVGL on Core 1
                if (emb) {
                    if (was_enrolling) {
                        // Multi-enrollment: append to the LAST user instead of
                        // creating a new identity each time. The user speaks the
                        // enroll command 3-5 times; all go to the same slot.
                        // Max-cosine verification then picks the best match
                        // across all enrollment samples — reducing EER 25-40%
                        // vs single-shot (Rajan et al., Interspeech 2019).
                        int slot = (g_voice_user_count > 0) ? (g_voice_user_count - 1) : 0;
                        if (g_voice_enroll_count[slot] >= MAX_ENROLL_PER_USER) {
                            ESP_LOGW(TAG, "Max %d enrollments reached for user %d", MAX_ENROLL_PER_USER, slot);
                            if (g_recognition_app) g_recognition_app->set_exec_text("Voice DB full!");
                        } else {
                            char label[32];
                            snprintf(label, sizeof(label), "voice_%d", slot);
                            g_voice_db->enroll(label, emb);
                            g_voice_db->build();
                            int e_idx = g_voice_enroll_count[slot];
                            if (!g_voice_embeddings[slot][e_idx])
                                g_voice_embeddings[slot][e_idx] = (float *)malloc(g_embedding_dim * sizeof(float));
                            if (g_voice_embeddings[slot][e_idx])
                                memcpy(g_voice_embeddings[slot][e_idx], emb, g_embedding_dim * sizeof(float));
                            g_voice_enroll_count[slot]++;
                            if (e_idx == 0) g_voice_user_count++;  // first enroll → new user
                            ESP_LOGI(TAG, "Voice enrolled: user=%d sample=%d/%d",
                                     slot, g_voice_enroll_count[slot], MAX_ENROLL_PER_USER);
                            g_voice_db->print();
                            if (g_recognition_app) {
                                char buf[48];
                                snprintf(buf, sizeof(buf), "User %d +%d/%d",
                                         slot + 1, g_voice_enroll_count[slot], MAX_ENROLL_PER_USER);
                                g_recognition_app->set_status_text(buf);
                                g_recognition_app->set_exec_text("Voice registered");
                            }
                        }
                        // Calibrate threshold after enrollment reaches 3+ samples
                        voice_calibrate_threshold();
                    } else {
                        g_voice_db->verify_max_cosine(emb, g_voice_threshold);
                        float best_score = 0.0f, second_best = 0.0f, min_score = 1.0f;
                        int best_user = -1;
                        char score_detail[128] = "";
                        int score_pos = 0;
                        // Max-cosine across ALL enrollment samples per user.
                        // Also log per-enrollment scores for gap analysis.
                        for (int i = 0; i < g_voice_user_count; i++) {
                            for (int e = 0; e < g_voice_enroll_count[i]; e++) {
                                if (!g_voice_embeddings[i][e]) continue;
                                float s = g_speaker_verifier->compute_similarity(emb, g_voice_embeddings[i][e]);
                                if (score_pos < (int)(sizeof(score_detail)-4))
                                    score_pos += snprintf(score_detail + score_pos,
                                        sizeof(score_detail) - score_pos,
                                        "%s%.3f", score_pos ? "," : "", (double)s);
                                if (s > best_score) { second_best = best_score; best_score = s; best_user = i; }
                                else if (s > second_best) { second_best = s; }
                                if (s < min_score) min_score = s;
                            }
                        }
                        int enrollers = g_voice_enroll_count[(best_user >= 0) ? best_user : 0];
                        float score_gap = best_score - second_best;
                        float score_range = best_score - min_score;
                        const char *result = (best_score > g_voice_threshold) ? "MATCH" : "NO MATCH";
                        ESP_LOGI(TAG, "Voice: user=%d score=%.4f gap=%.3f range=%.3f -> %s [%s]",
                                 best_user, best_score, (double)score_gap, (double)score_range,
                                 result, score_detail);

                        // Execute or reject pending command (combined face+voice)
                        int pending = g_pending_cmd_id;
                        g_pending_cmd_id = 0;
                        // ── Two-tier voice auth ──
                        // Tier 1: score > 0.70 → confident genuine, no further checks.
                        // Tier 2: score > threshold but < 0.70 → suspicious zone.
                        //    Requires score consistency across ALL enrollments.
                        //    Genuine users match all enrollments similarly (gap < 0.15);
                        //    imposters often match one enrollment by chance but not others.
                        bool voice_ok = false;
                        if (best_user >= 0 && best_score > g_voice_threshold) {
                            if (best_score > 0.70f) {
                                voice_ok = true;  // confident match
                            } else if (enrollers >= 3 && score_gap < 0.18f) {
                                // Marginal score — must be consistent across enrollments
                                voice_ok = true;
                                ESP_LOGI(TAG, "Voice: marginal score=%.4f but consistent (gap=%.3f) → OK",
                                         (double)best_score, (double)score_gap);
                            } else if (enrollers < 3) {
                                voice_ok = true;  // not enough data for consistency check
                            } else {
                                ESP_LOGW(TAG, "Voice: score=%.4f gap=%.3f → REJECTED (inconsistent)",
                                         (double)best_score, (double)score_gap);
                            }
                        }

                        // Multi-frame face voting: if first attempt fails, re-trigger
                        // RECOGNIZE up to 2 more times. Because the user enrolled ALL
                        // faces as themselves, take the highest-scoring match across all
                        // IDs (max-cosine semantics).
                        bool face_ok = false;
                        float best_face_sim = 0;
                        int   best_face_id = -1;
                        for (int face_try = 0; face_try < 5; face_try++) {
                            char fc[64]; face_result_copy(fc, sizeof(fc));
                            if (g_recog_event_group && fc[0] == '\0') {
                                vTaskDelay(pdMS_TO_TICKS(300));
                                face_result_copy(fc, sizeof(fc));
                            }
                            if (strncmp(fc, "id:", 3) == 0) {
                                int fid = -1; float fs = 0;
                                if (sscanf(fc, "id: %d, sim: %f", &fid, &fs) == 2) {
                                    if (fs > best_face_sim) { best_face_sim = fs; best_face_id = fid; }
                                    face_ok = true;
                                }
                            }
                            if (face_ok && face_try < 4 && best_face_sim < 0.5f) {
                                portENTER_CRITICAL(&g_face_spinlock);
                                g_last_recog_face[0] = '\0';
                                portEXIT_CRITICAL(&g_face_spinlock);
                                xEventGroupSetBits(g_recog_event_group, 32);
                                vTaskDelay(pdMS_TO_TICKS(300));
                            } else {
                                break;
                            }
                        }
                        if (face_ok) {
                            portENTER_CRITICAL(&g_face_spinlock);
                            snprintf(g_last_recog_face, sizeof(g_last_recog_face),
                                     "id: %d, sim: %.2f", best_face_id, (double)best_face_sim);
                            portEXIT_CRITICAL(&g_face_spinlock);
                        }
                        g_face_triggered = false;

                        // Geometric mean: sqrt(face × voice). Single-modality
                        // failure (score≈0) forces the product to zero naturally.
                        float fusion = sqrtf(best_face_sim * best_score);
                        bool authorized = (fusion > FUSION_THRESHOLD);
                        // CSI liveness gate: requires breathing human detected.
                        // Bypassed until first successful calibration.
                        static bool csi_calibrated = false;
                        if (csi_liveness_check()) csi_calibrated = true;
                        if (csi_calibrated && !csi_liveness_check()) {
                            authorized = false;
                            ESP_LOGW(TAG, "CSI liveness: no breathing detected — auth blocked");
                        }

                        if (g_recognition_app) {
                            char buf[80];
                            if (pending == 7) {
                                snprintf(buf, sizeof(buf), "User %d: %.2f", best_user + 1, best_score);
                            } else if (pending >= 1 && pending <= 5) {
                                snprintf(buf, sizeof(buf), "%s: √(%.2f×%.2f)=%.2f",
                                         authorized ? "ALLOW" : "ALARM",
                                         (double)best_face_sim, (double)best_score,
                                         (double)fusion);
                            }
                            g_recognition_app->set_exec_text(buf);
                            face_app_show_auth(authorized ? "ALLOW" : "ALARM", authorized);
                        }
                        // Report event for commands 1-5
                        if (pending >= 1 && pending <= 5) {
                            char jb[128];
                            snprintf(jb, sizeof(jb),
                                     "\"cmd_id\":%d,\"voice\":\"%.2f\",\"face\":\"%.2f\",\"fusion\":\"%.2f\",\"result\":\"%s\"",
                                     pending,
                                     (double)best_score, (double)best_face_sim,
                                     (double)fusion,
                                     authorized ? "allow" : "alarm");
                            report_event("voice_command", jb);
                        }
                    }
                    free(emb);
                } else {
                    ESP_LOGE(TAG, "Speaker verification failed.");
                    if (g_recognition_app) g_recognition_app->set_exec_text("Voice fail");
                }
            } else {
                ESP_LOGW(TAG, "Could not acquire mutex for speaker verification.");
                g_voice_enrolling = false;
                g_voice_verifying = false;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        // ---- Simple energy-based VAD: only feed speech to MultiNet ----
        float frame_energy = 0;
        for (int i = 0; i < chunksize; i++) frame_energy += (float)audio_buf[i] * audio_buf[i];
        frame_energy /= chunksize;  // mean squared value

        // Update noise floor (slow decay, fast rise)
        if (frame_energy < g_voice_audio.noise_floor || g_voice_audio.noise_floor == 0) {
            g_voice_audio.noise_floor = frame_energy;
        } else {
            g_voice_audio.noise_floor += (frame_energy - g_voice_audio.noise_floor) * 0.001f;
        }

        // Hard ceiling: noise floor should never exceed a reasonable max (~20K).
        // A single loud event can spike it, and the 0.001 decay takes minutes
        // to bring it back down — effectively silencing all commands until then.
        if (g_voice_audio.noise_floor > 20000.0f) {
            g_voice_audio.noise_floor = 20000.0f;
        }

        // Periodic re-baseline: every ~30s, if the room is quiet, snap the
        // noise floor to the minimum observed energy in the last second.
        // This prevents permanent drift from events like door slams.
        static int recal_frames = 0;
        static float recal_min = 1e9f;
        recal_frames++;
        if (frame_energy < recal_min) recal_min = frame_energy;
        if (recal_frames >= 30000 / 32) {  // ~30 seconds
            if (recal_min < g_voice_audio.noise_floor * 0.5f) {
                ESP_LOGI(TAG, "VAD: recalibrating noise floor %.0f → %.0f",
                         (double)g_voice_audio.noise_floor, (double)recal_min);
                g_voice_audio.noise_floor = recal_min;
            }
            recal_frames = 0;
            recal_min = 1e9f;
        }

        // Speech detection: energy > 3× noise floor
        bool is_speech_frame = (frame_energy > g_voice_audio.noise_floor * 3.0f &&
                                frame_energy > 100.0f);  // absolute minimum
        if (is_speech_frame) {
            g_voice_audio.silence_frames = 0;
            g_voice_audio.speech_frames++;
        } else {
            g_voice_audio.speech_frames = 0;
            g_voice_audio.silence_frames++;
        }

        // Hysteresis: need 3 frames to start, 10 frames to stop
        if (g_voice_audio.speech_frames >= 3 && !g_voice_audio.is_speaking) {
            g_voice_audio.is_speaking = true;
            ESP_LOGD(TAG, "Voice: VAD ON (energy=%.0f, noise=%.0f)",
                     frame_energy, g_voice_audio.noise_floor);
        } else if (g_voice_audio.silence_frames >= 10 && g_voice_audio.is_speaking) {
            g_voice_audio.is_speaking = false;
            ESP_LOGD(TAG, "Voice: VAD OFF");
        }

        if (g_skip_detect_count > 0) {
            g_skip_detect_count = g_skip_detect_count - 1;
        } else if (xSemaphoreTake(g_espdl_mutex, pdMS_TO_TICKS(100))) {
            esp_mn_state_t state = multinet->detect(mn_data, audio_buf);
            if (state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *r = multinet->get_results(mn_data);
                const char *cmd = "Cmd: Unknown";
                int cmd_id = r->command_id[0];
                switch (cmd_id) {
                    case 1: cmd = "Cmd: Open TV"; break;
                    case 2: cmd = "Cmd: Close TV"; break;
                    case 3: cmd = "Cmd: Open Door"; break;
                    case 4: cmd = "Cmd: Close Door"; break;
                    case 5: cmd = "Cmd: Identify"; break;
                    case 6: cmd = "Cmd: Enroll Voice"; break;
                    case 7: cmd = "Cmd: Verify Voice"; break;
                    case 8: cmd = "Cmd: Clear Voices"; break;
                }
                ESP_LOGI(TAG, "Detected: %s (prob=%.2f)", cmd,
                         r->num > 0 ? r->prob[0] : 0.0f);

                // Voice enrollment / verification commands
                if (cmd_id == 6 && (g_speaker_verifier || g_voice_remote_mode) && g_voice_cap_buf) {
                    if (!g_voice_enroll_allowed) {
                        // NOTE: was `return;` — that EXITED the whole voice task,
                        // silently killing ALL voice commands until reboot!
                        if (g_recognition_app) g_recognition_app->set_exec_text("Enroll disabled");
                    } else {
                    g_voice_enrolling = true;
                    g_voice_verifying = false;
                    g_voice_collect_samples = 0;
                    // 4s post-detect delay: ensures user has time to react and start speaking.
                    // The command phrase ("zhu ce sheng wen") should be fully outside the capture
                    // window so the model only hears clean speaker identity speech.
                    g_post_detect_samples = 16000 * 4; // 4s delay → clean post-command speech
                    if (g_recognition_app) {
                        g_recognition_app->set_status_text("Enrolling voice... speak now");
                        g_recognition_app->set_exec_text("Recording...");
                    }
                    }
                } else if (cmd_id == 8) {
                    // Clear all voice enrollments
                    if (g_voice_db) g_voice_db->clear();
                    for (int i = 0; i < MAX_VOICE_USERS; i++) {
                        for (int e = 0; e < MAX_ENROLL_PER_USER; e++) {
                            free(g_voice_embeddings[i][e]);
                            g_voice_embeddings[i][e] = nullptr;
                        }
                        g_voice_enroll_count[i] = 0;
                    }
                    g_voice_user_count = 0;
                    ESP_LOGI(TAG, "All voice enrollments cleared.");
                    if (g_recognition_app) {
                        g_recognition_app->set_status_text("Voices cleared");
                        g_recognition_app->set_exec_text("DB empty");
                    }
                } else if (cmd_id == 7 && (g_speaker_verifier || g_voice_remote_mode) && (g_voice_db || g_voice_remote_mode)) {
                    g_voice_verifying = true;
                    g_voice_enrolling = false;
                    g_voice_collect_samples = 0;
                    g_post_detect_samples = 16000 * 4; // 4s delay → clean post-command speech
                    if (g_recognition_app) {
                        g_recognition_app->set_status_text("Verify voice... speak now");
                        g_recognition_app->set_exec_text("Recording...");
                    }
                } else if ((g_voice_user_count > 0 || g_voice_remote_mode) && g_voice_cap_buf && (g_speaker_verifier || g_voice_remote_mode)) {
                    // Commands 1-5, 7: immediate voice + face verification
                    if (g_recog_event_group && cmd_id <= 5) {
                        g_last_recog_face[0] = '\0';     // clear stale face result
                        g_skip_detect_count = 3;  // 3 frames (~90ms) — enough to skip trigger tail
                        xEventGroupSetBits(g_recog_event_group, 32);
                        g_face_triggered = true;          // wait for fresh face result
                    }
                    // Snapshot rolling buffer immediately (utterance already in buffer)
                    int available = (g_rolling_total >= ROLLING_BUF_SAMPLES) ? ROLLING_BUF_SAMPLES : g_rolling_total;
                    int max_samples = 16000 * 3;  // 3s window
                    int to_copy = (available < max_samples) ? available : max_samples;
                    int start = (g_rolling_wr - to_copy + ROLLING_BUF_SAMPLES) % ROLLING_BUF_SAMPLES;
                    for (int i = 0; i < to_copy; i++)
                        g_voice_cap_buf[i] = g_rolling_buf[(start + i) % ROLLING_BUF_SAMPLES];
                    g_voice_collect_samples = to_copy;
                    g_voice_verifying = true;
                    g_pending_cmd_id = cmd_id;
                    ESP_LOGI(TAG, "Voice verify snapshot for cmd %d, %d samples", cmd_id, to_copy);
                    // Reset ring buffer to prevent old commands from leaking into next capture
                    g_rolling_total = 0;
                    g_rolling_wr = 0;
                    if (g_recognition_app) {
                        g_recognition_app->set_status_text(cmd);
                        g_recognition_app->set_exec_text("Verifying voice...");
                    }
                } else {
                    // No voice users enrolled — fallback to face-only auth
                    // Multi-frame voting: re-trigger RECOGNIZE up to 2 more times
                    // if first attempt fails. All faces are the same person, so
                    // take the highest-scoring match.
                    float best_fs = 0; int best_fid = -1;
                    for (int face_try = 0; face_try < 5; face_try++) {
                        if (face_try == 0) {
                            if (g_recog_event_group) {
                                g_skip_detect_count = 3;
                                xEventGroupSetBits(g_recog_event_group, 32);
                            }
                        }
                        // 300ms: ESP-DET-PICO detect(~54ms) + feature extraction(~100ms)
                        // + DB query(~50ms) + margin for frame timing jitter
                        vTaskDelay(pdMS_TO_TICKS(300));
                        char fc[64]; face_result_copy(fc, sizeof(fc));
                        if (strncmp(fc, "id:", 3) == 0) {
                            int fid = -1; float fs = 0;
                            if (sscanf(fc, "id: %d, sim: %f", &fid, &fs) == 2 && fs > best_fs) {
                                best_fs = fs; best_fid = fid;
                            }
                            if (best_fs >= 0.5f) break;  // good enough
                        }
                        if (face_try < 4) {
                            portENTER_CRITICAL(&g_face_spinlock);
                            g_last_recog_face[0] = '\0';
                            portEXIT_CRITICAL(&g_face_spinlock);
                            if (g_recog_event_group) xEventGroupSetBits(g_recog_event_group, 32);
                        }
                    }
                    bool authorized = (best_fs > FUSION_THRESHOLD);
                    static bool csi_cal2 = false;
                    if (csi_liveness_check()) csi_cal2 = true;
                    if (csi_cal2 && !csi_liveness_check()) authorized = false;
                    if (authorized) {
                        portENTER_CRITICAL(&g_face_spinlock);
                        snprintf(g_last_recog_face, sizeof(g_last_recog_face),
                                 "id: %d, sim: %.2f", best_fid, (double)best_fs);
                        portEXIT_CRITICAL(&g_face_spinlock);
                    }
                    if (g_recognition_app) {
                        g_recognition_app->set_status_text(cmd);
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%s: F=%.2f",
                                 authorized ? "Allow" : "Alarm", (double)best_fs);
                        g_recognition_app->set_exec_text(buf);
                        face_app_show_auth(authorized ? "ALLOW" : "ALARM", authorized);
                    }
                }
                // Reset model for next utterance (like xiaozhi CustomWakeWord does)
                multinet->clean(mn_data);
            } else if (state == ESP_MN_STATE_TIMEOUT) {
                // MultiNet timed out (no command matched within duration window).
                // Must clean state machine or it stays stuck in TIMEOUT forever.
                ESP_LOGD(TAG, "MultiNet timeout, resetting state");
                multinet->clean(mn_data);
            }
            xSemaphoreGive(g_espdl_mutex);
        } else {
            // Mutex held by face recognition — audio frame dropped
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        taskYIELD();
    }
    free(audio_buf);
    vTaskDelete(NULL);
}

static void on_clock_update_cb(lv_timer_t *timer)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    auto *phone = (ESP_Brookesia_Phone *)timer->user_data;
    phone->getHome().getStatusBar()->setClock(timeinfo.tm_hour, timeinfo.tm_min);

    // Apply WiFi icon state set by uart_rx_task (from S3)
    static int last_wifi = -1;
    if (g_wifi_icon_state != last_wifi) {
        last_wifi = g_wifi_icon_state;
        phone->getHome().getStatusBar()->setWifiIconState(last_wifi);
    }
}

extern "C" void app_main(void)
{
    // ====================================================================
    // Phase 1: NVS + Event Loop
    // ====================================================================
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    vTaskPrioritySet(xTaskGetCurrentTaskHandle(), 5);

    // Timezone for clock display — S3 sends UTC epoch via UART, localtime_r()
    // needs TZ to render Beijing time (was lost when SNTP code was removed)
    setenv("TZ", "CST-8", 1);
    tzset();

    // Light up backlight as power-on indicator (display content comes later via esp-who)
    gpio_set_direction(GPIO_NUM_20, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_20, 1);

    // ====================================================================
    // Phase 2: Init event loop (needed by esp-who components)
    // ====================================================================
    // esp_netif_init();  // C5 removed
    esp_event_loop_create_default();

    // ── Ethernet (IP101, RMII, pins 31/52/51) ──
    // DIAGNOSTIC STEP 3: disable Ethernet to test DHCP theory
#if 0
    ESP_ERROR_CHECK(bsp_eth_init());
    ESP_LOGI(TAG, "Ethernet init successfully");
#endif

    // ── UART bridge to S3 (GPIO4/5, 921600 baud) ──
    uart_bridge_init();
    // ── CSI liveness detector (analyzes ADR-018 frames for respiration) ──
    csi_liveness_init();
    // ── Radar status from S3 via UART → LVGL display (task waits for LVGL) ──
    radar_display_init();

    // ====================================================================
    // Phase 3: Filesystem + Camera + Audio + Recognition (WiFi later on click)
    // ====================================================================
#if CONFIG_DB_FATFS_FLASH
    ESP_ERROR_CHECK(fatfs_flash_mount());
#elif CONFIG_DB_SPIFFS
    ESP_ERROR_CHECK(bsp_spiffs_mount());
#endif
#if CONFIG_IDF_TARGET_ESP32S3
    auto frame_cap = get_dvp_frame_cap_pipeline();
#elif CONFIG_IDF_TARGET_ESP32P4
    auto frame_cap = get_mipi_csi_frame_cap_pipeline();
#endif

    // ---- Init Audio ----
    {
        i2s_std_config_t audio_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
            .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = { .mclk = GPIO_NUM_13, .bclk = GPIO_NUM_12, .ws = GPIO_NUM_10,
                          .dout = GPIO_NUM_9, .din = GPIO_NUM_11,
                          .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false } },
        };
        ESP_ERROR_CHECK(bsp_audio_init(&audio_cfg));
        esp_codec_dev_handle_t speaker = bsp_audio_codec_speaker_init();
        g_speaker_handle = speaker;
        g_mic_handle = bsp_audio_codec_microphone_init();

        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16, .channel = 2, .channel_mask = 1,
            .sample_rate = 16000, .mclk_multiple = 256,
        };
        esp_codec_dev_open(speaker, &fs);
        esp_codec_dev_open(g_mic_handle, &fs);
        esp_codec_dev_set_in_gain(g_mic_handle, 25.0);  // Prevent ADC clipping
        esp_codec_dev_set_out_vol(speaker, 70);  // set speaker volume (matches xiaozhi-esp32)
        ESP_LOGI(TAG, "🎵 Audio: BSP init complete");
        ESP_LOGI(TAG, "🎵 I2S config: PORT=%d rate=%d bits=%d ch=%d mask=0x%x mclk_mult=%d",
                 CONFIG_BSP_I2S_NUM, fs.sample_rate, fs.bits_per_sample,
                 fs.channel, fs.channel_mask, fs.mclk_multiple);
        ESP_LOGI(TAG, "🎵 Handles: mic=%p speaker=%p",
                 (void *)g_mic_handle, (void *)g_speaker_handle);
        ESP_LOGI(TAG, "🎵 GPIO: MCLK=%d BCLK=%d WS=%d DOUT=%d DIN=%d",
                 GPIO_NUM_13, GPIO_NUM_12, GPIO_NUM_10, GPIO_NUM_9, GPIO_NUM_11);
    }

    // ---- Load Voice Model ----
    voice_task_params_t voice_params = {};
    {
        vTaskPrioritySet(NULL, 1);
        srmodel_list_t *models = esp_srmodel_init("model");
        if (models && models->num > 0) {
            char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, "cn");
            if (mn_name) {
                ESP_LOGI(TAG, "MultiNet model: %s", mn_name);
                voice_params.multinet = esp_mn_handle_from_name(mn_name);
                voice_params.mn_data = voice_params.multinet->create(mn_name, 6000);
                voice_params.multinet->set_det_threshold(voice_params.mn_data, 0.35);
                esp_mn_commands_add(1, "da kai dian shi");
                esp_mn_commands_add(2, "guan bi dian shi");
                esp_mn_commands_add(3, "da kai men");
                esp_mn_commands_add(4, "guan bi men");
                esp_mn_commands_add(5, "shi bie ren lian");
                esp_mn_commands_add(6, "zhu ce sheng wen");
                esp_mn_commands_add(7, "shi bie sheng yin");
                esp_mn_commands_add(8, "qing chu sheng wen");
                esp_mn_commands_update();
                voice_params.chunksize = voice_params.multinet->get_samp_chunksize(voice_params.mn_data);
            }
        }
        vTaskPrioritySet(NULL, 5);
    }

    // ---- Mount SD card via SPI (with internal LDO power for UHS-I pins) ----
    {
        ESP_LOGI(TAG, "Mounting SD card (SDSPI, pins 39/42/43/44)...");

        esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 64 * 1024,
        };
        sdmmc_card_t *card = NULL;

        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        // 20MHz reads corrupt data under PSRAM/DMA contention (LVGL+camera active).
        // SPI mode has no data CRC — corruption is silent. 10MHz is reliable.
        host.max_freq_khz = 10000;

        // Internal LDO power for UHS-I pins (39-44)
        sd_pwr_ctrl_ldo_config_t ldo_cfg = { .ldo_chan_id = 4 };
        sd_pwr_ctrl_handle_t pwr_ctrl = NULL;
        if (sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &pwr_ctrl) == ESP_OK) {
            host.pwr_ctrl_handle = pwr_ctrl;
        }

        spi_bus_config_t bus_cfg = {
            .mosi_io_num = 44,  // CMD
            .miso_io_num = 39,  // D0
            .sclk_io_num = 43,  // CLK
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4000,
        };
        esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SDSPI bus init failed: %s", esp_err_to_name(ret));
        } else {
            sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
            slot_cfg.gpio_cs = GPIO_NUM_42;  // D3
            slot_cfg.host_id = (spi_host_device_t)host.slot;

            ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_cfg, &mount_cfg, &card);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "SD card mount failed: 0x%x (%s)", ret, esp_err_to_name(ret));
            } else {
                ESP_LOGI(TAG, "SD card mounted at /sdcard");
                sdmmc_card_print_info(stdout, card);
            }
        }
    }

    // ---- Initialize LVGL + Brookesia Phone UI ----
    // Set skip flag BEFORE creating recognition app (prevents WhoLCD from re-initializing LVGL)
    who::lcd::WhoLCD::s_skip_hw_init = true;

    // Initialize LVGL display (same config as WhoLCD::init for ESP32P4)
    {
        lvgl_port_cfg_t lvgl_port_cfg = {
            .task_priority = 5,
            .task_stack = 8192,
            .task_affinity = 1,   // Core 1 only: separate from voice/model on Core 0
            .task_max_sleep_ms = 500,
            .timer_period_ms = 5,
        };
        // Partial refresh: single 50-line draw buffer (~100KB) instead of
        // dual full-screen buffers (2.4MB).  Pairs with DPI buffer 2→1 in
        // sdkconfig.  Total saving: ~3.4MB PSRAM.
        bsp_display_cfg_t cfg = {
            .lvgl_port_cfg = lvgl_port_cfg,
            .buffer_size = BSP_LCD_H_RES * 50,   // 50-line partial buffer
            .double_buffer = 0,
            .hw_cfg = {
                .dsi_bus = {
                    .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
                    .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
                }
            },
            .flags = {
                .buff_dma = true,
                .buff_spiram = true,
                .sw_rotate = false,
            }
        };
        lv_display_t *disp = bsp_display_start_with_config(&cfg);
        // FULL render mode with SMALL buffer: LVGL renders the whole screen
        // in stripes through the 50-line buffer.  This preserves correct
        // mirror/swap handling while using 100KB instead of 2.4MB PSRAM.
        // (Pure partial refresh breaks on MIPI DSI panels with mirror_x.)
        lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_FULL);
        bsp_display_backlight_on();

        // Force GPIO 20 as backlight control (override BSP default GPIO 26)
        gpio_set_direction(GPIO_NUM_20, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_NUM_20, 1);

        // Create Phone UI shell
        bsp_display_lock(0);
        g_phone = new ESP_Brookesia_Phone(disp);
        auto *stylesheet = new ESP_Brookesia_PhoneStylesheet_t(
            ESP_BROOKESIA_PHONE_1024_600_DARK_STYLESHEET());

        // Try loading wallpaper: NVS path → /sdcard/wallpaper.rgb565 → default
        {
            const void *wp_resource = nullptr;  // fallback: brookesia default

            // Helper: load a .rgb565 file into lv_image_dsc_t
            auto try_load = [](const char *path) -> lv_image_dsc_t * {
                FILE *f = fopen(path, "rb");
                if (!f) return nullptr;
                fseek(f, 0, SEEK_END);
                long sz = ftell(f);
                if (sz != 1024 * 600 * 2) { fclose(f); return nullptr; }
                void *data = malloc(sz);
                if (!data) { fclose(f); return nullptr; }
                fseek(f, 0, SEEK_SET);
                fread(data, 1, sz, f);
                fclose(f);
                lv_image_dsc_t *dsc = (lv_image_dsc_t *)calloc(1, sizeof(lv_image_dsc_t));
                if (!dsc) { free(data); return nullptr; }
                dsc->header.cf     = LV_COLOR_FORMAT_RGB565;
                dsc->header.w      = 1024;
                dsc->header.h      = 600;
                dsc->header.stride = 2048;
                dsc->header.magic  = LV_IMAGE_HEADER_MAGIC;
                dsc->data           = (const uint8_t *)data;
                dsc->data_size      = (uint32_t)sz;
                g_active_wp_dsc  = dsc;
                g_active_wp_data = data;
                return dsc;
            };

            // Step 1: check NVS for user-chosen wallpaper
            nvs_handle_t nvs;
            if (nvs_open("wallpaper", NVS_READONLY, &nvs) == ESP_OK) {
                char path[256] = {0};
                size_t len = sizeof(path);
                if (nvs_get_str(nvs, "path", path, &len) == ESP_OK
                    && strcmp(path, "default") != 0) {
                    lv_image_dsc_t *dsc = try_load(path);
                    if (dsc) {
                        wp_resource = dsc;
                        ESP_LOGI(TAG, "Wallpaper from NVS: %s", path);
                    }
                }
                nvs_close(nvs);
            }

            // Step 2: if no wallpaper yet, try SD card wallpaper.rgb565
            if (!wp_resource) {
                lv_image_dsc_t *dsc = try_load("/sdcard/wallpaper.rgb565");
                if (dsc) {
                    wp_resource = dsc;
                    ESP_LOGI(TAG, "Wallpaper from SD card: /sdcard/wallpaper.rgb565");
                }
            }

            if (wp_resource) {
                stylesheet->core.home.background.wallpaper_image_resource =
                    ESP_BROOKESIA_STYLE_IMAGE(wp_resource);
            }  // else: brookesia uses its built-in default wallpaper
        }

        g_phone->addStylesheet(stylesheet);
        g_phone->activateStylesheet(stylesheet);
        delete stylesheet;

        g_phone->setTouchDevice(bsp_display_get_input_dev());
        g_phone->registerLvLockCallback(
            (ESP_Brookesia_GUI_LockCallback_t)(bsp_display_lock), 0);
        g_phone->registerLvUnlockCallback(
            (ESP_Brookesia_GUI_UnlockCallback_t)(bsp_display_unlock));
        g_phone->begin();

        // Install Camera App
        auto *camera_app = new FaceRecognitionApp();
        g_phone->installApp(camera_app);

        // Install Settings App
        auto *settings_app = new SettingsApp();
        g_phone->installApp(settings_app);

        // Install XiaoZhi App
        auto *xiaozhi_app = new XiaoZhiApp();
        g_phone->installApp(xiaozhi_app);

        // Clock update timer
        lv_timer_create(on_clock_update_cb, 1000, g_phone);

        // Time sync: P4 has no network. S3 syncs time via WiFi SNTP and sends
        // {"dev":"s3","time":<unix_ts>} over UART. uart_rx_task calls settimeofday().

        bsp_display_unlock();

        // === DIAGNOSTIC STEP 5: re-enable face pipeline ONLY ===
        ESP_LOGI(TAG, "DIAGNOSTIC STEP 5: face pipeline only (no speaker verif, no voice)");
    }

    // ---- Create Recognition App (pipeline only, UI created in FaceRecognitionApp::run()) ----
    auto recognition_app = new WhoRecognitionAppLCD(frame_cap);
    g_recognition_app = recognition_app;

    g_espdl_mutex = xSemaphoreCreateMutex();
    g_remote_score_sem = xSemaphoreCreateBinary();
    uart_bridge_on_frame(on_voice_score_cb);

    // ---- Speaker Verification Init (instrumented fast-load path) ----
    {
        ESP_LOGI(TAG, "Init speaker verification (fast SD→PSRAM load)...");
        // 6s model: longer window = better inter-speaker discrimination.
        // 3s had EER≈4.5% on clean data; real-world with embedded mic is
        // worse → strangers occasionally matched. 6s captures more voice
        // characteristics for stronger separation.
        g_speaker_verifier = new SpeakerVerification(3);
        g_embedding_dim = g_speaker_verifier->get_embedding_dim();
        if (g_embedding_dim > 0) {
            g_voice_db = new dl::feat::FeatVerificationDatabase("/sdcard/voice.db", g_embedding_dim);
            if (!g_voice_db->is_valid()) {
                ESP_LOGW(TAG, "Voice DB init failed, creating new one...");
            }
            // Populate g_voice_embeddings[][] from persistent DB.
            // Load ALL enrollment samples per user (not just the first).
            g_voice_user_count = 0;
            if (g_voice_db->is_valid()) {
                auto labels = g_voice_db->get_labels();
                for (const auto &label : labels) {
                    auto embeddings = g_voice_db->get_embeddings(label);
                    if (!embeddings.empty() && g_voice_user_count < MAX_VOICE_USERS) {
                        int slot = g_voice_user_count;
                        int count = 0;
                        for (const auto &emb : embeddings) {
                            if (count >= MAX_ENROLL_PER_USER) break;
                            if (!g_voice_embeddings[slot][count])
                                g_voice_embeddings[slot][count] = (float *)malloc(g_embedding_dim * sizeof(float));
                            if (g_voice_embeddings[slot][count])
                                memcpy(g_voice_embeddings[slot][count], emb.data(), g_embedding_dim * sizeof(float));
                            count++;
                        }
                        g_voice_enroll_count[slot] = count;
                        g_voice_user_count++;
                    }
                }
                ESP_LOGI(TAG, "Loaded %d voice user(s) from database", g_voice_user_count);
                voice_calibrate_threshold();  // re-calibrate from persisted embeddings
            }
            g_voice_cap_buf = (int16_t *)heap_caps_malloc(16000 * 6 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
            ESP_LOGI(TAG, "Speaker verification ready (emb=%d dims)", g_embedding_dim);
        } else {
            ESP_LOGE(TAG, "Speaker verification model failed to load — voice cmd still works (face-only auth)");
        }
    }

    // Rolling audio buffer (5s ring) — required by voice task for command capture
    // AND speaker verification snapshots. Allocate regardless of SV model status.
    g_rolling_buf = (int16_t *)heap_caps_malloc(ROLLING_BUF_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!g_rolling_buf) {
        ESP_LOGE(TAG, "Failed to allocate 5s rolling audio buffer");
    }

    // Voice command recognition (MultiNet)
    if (voice_params.multinet) {
        voice_task_params_t *p = (voice_task_params_t *)malloc(sizeof(voice_task_params_t));
        if (p) {
            *p = voice_params;
            xTaskCreatePinnedToCore(voice_recognition_task, "voice_cmd", 12288, p, 3, NULL, 0);
        } else {
            ESP_LOGE(TAG, "Failed to allocate voice task params");
        }
    }

    // === DIAGNOSTIC STEP 5: face pipeline re-enabled ===
    recognition_app->run();
}
