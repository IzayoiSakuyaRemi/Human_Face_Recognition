#include "frame_cap_pipeline.hpp"
#include "who_recognition_app_lcd.hpp"
#include "who_recognition_app_term.hpp"
#include "who_spiflash_fatfs.hpp"
#include "event_reporter.hpp"
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
#include "esp_sntp.h"
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
static ESP_Brookesia_Phone *g_phone = nullptr;
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

// Speaker verification — multi-user with rolling audio buffer
#define MAX_VOICE_USERS 10
#define ROLLING_BUF_SECS      5
#define ROLLING_BUF_SAMPLES   (16000 * ROLLING_BUF_SECS)  // 80000
static SpeakerVerification *g_speaker_verifier = nullptr;
static dl::feat::FeatVerificationDatabase *g_voice_db = nullptr;
bool g_voice_enrolling = false;
bool g_voice_verifying = false;
static int g_voice_collect_samples = 0;
static int16_t *g_voice_cap_buf = nullptr;   // captured audio for verification
static float *g_voice_embeddings[MAX_VOICE_USERS] = {};
static int g_voice_user_count = 0;
static int g_embedding_dim = 0;
// Rolling circular buffer: always keeps last 5s of audio
static int16_t *g_rolling_buf = nullptr;
static int g_rolling_wr = 0;       // write position (circular)
static int g_rolling_total = 0;    // total samples written (for cold-start check)
static int g_post_detect_samples = 0;
int g_pending_cmd_id = 0;
static bool g_face_triggered = false;  // face detection was requested for pending cmd

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

    // BSP codec may configure I2S as MONO or STEREO depending on version.
    // We read chunksize mono samples and let the driver handle the format.
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
            ESP_LOGI(TAG, "🎙 Voice task RESUMED, hwm=%lu",
                     uxTaskGetStackHighWaterMark(NULL));
            was_paused = false;
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
            ESP_LOGI(TAG, "Voice: %d reads/sec, nonzero=%d/%d", read_count, nz, chunksize);
            read_count = 0;
            last_log = now;
        }

        // ---- Simple AGC: normalize audio volume ----
        apply_agc(audio_buf, chunksize, 2000.0f);

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
                // Snapshot last 3s from rolling buffer
                int available = (g_rolling_total >= ROLLING_BUF_SAMPLES) ? ROLLING_BUF_SAMPLES : g_rolling_total;
                int max_samples = 16000 * 3;
                int to_copy = (available < max_samples) ? available : max_samples;
                int start = (g_rolling_wr - to_copy + ROLLING_BUF_SAMPLES) % ROLLING_BUF_SAMPLES;
                for (int i = 0; i < to_copy; i++)
                    g_voice_cap_buf[i] = g_rolling_buf[(start + i) % ROLLING_BUF_SAMPLES];
                g_voice_collect_samples = to_copy;
                ESP_LOGI(TAG, "Voice: captured %d samples from rolling buffer", to_copy);
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
            if (xSemaphoreTake(g_espdl_mutex, pdMS_TO_TICKS(100))) {
                float *emb = g_speaker_verifier->run(g_voice_cap_buf, total);
                xSemaphoreGive(g_espdl_mutex);
                vTaskDelay(pdMS_TO_TICKS(5));  // yield to LVGL on Core 1
                if (emb) {
                    if (was_enrolling) {
                        // Multi-user: auto-increment slot
                        int slot = g_voice_user_count;
                        if (slot >= MAX_VOICE_USERS) {
                            ESP_LOGW(TAG, "Max voice users (%d) reached!", MAX_VOICE_USERS);
                            if (g_recognition_app) g_recognition_app->set_exec_text("Voice DB full!");
                        } else {
                            char label[32];
                            // Find next available slot (avoid collisions with existing labels)
                            while (slot < MAX_VOICE_USERS) {
                                snprintf(label, sizeof(label), "voice_%d", slot);
                                if (g_voice_db->get_embeddings(label).empty()) break;
                                slot++;
                            }
                            snprintf(label, sizeof(label), "voice_%d", slot);
                            g_voice_db->enroll(label, emb);
                            g_voice_db->build();
                            if (!g_voice_embeddings[slot])
                                g_voice_embeddings[slot] = (float *)malloc(g_embedding_dim * sizeof(float));
                            memcpy(g_voice_embeddings[slot], emb, g_embedding_dim * sizeof(float));
                            g_voice_user_count++;
                            ESP_LOGI(TAG, "Voice enrolled as %s (slot %d/%d)", label, slot, MAX_VOICE_USERS);
                            g_voice_db->print();
                            if (g_recognition_app) {
                                char buf[48];
                                snprintf(buf, sizeof(buf), "User %d enrolled", slot + 1);
                                g_recognition_app->set_status_text(buf);
                                g_recognition_app->set_exec_text("Voice registered");
                            }
                        }
                    } else {
                        g_voice_db->verify_max_cosine(emb, 0.25f);
                        float best_score = 0.0f;
                        int best_user = -1;
                        for (int i = 0; i < g_voice_user_count; i++) {
                            if (!g_voice_embeddings[i]) continue;
                            float s = g_speaker_verifier->compute_similarity(emb, g_voice_embeddings[i]);
                            if (s > best_score) { best_score = s; best_user = i; }
                        }
                        const char *result = (best_score > 0.25f) ? "MATCH" : "NO MATCH";
                        ESP_LOGI(TAG, "Voice: user=%d score=%.4f -> %s (checked %d users)",
                                 best_user, best_score, result, g_voice_user_count);

                        // Execute or reject pending command (combined face+voice)
                        int pending = g_pending_cmd_id;
                        g_pending_cmd_id = 0;
                        bool voice_ok = (best_user >= 0 && best_score > 0.25f);
                        bool face_ok = (g_face_triggered && strncmp(g_last_recog_face, "id:", 3) == 0);
                        g_face_triggered = false;

                        if (g_recognition_app) {
                            char buf[80];
                            if (pending == 7) {
                                snprintf(buf, sizeof(buf), "User %d: %.2f", best_user + 1, best_score);
                            } else if (pending >= 1 && pending <= 5) {
                                // Combined face + voice authorization
                                if (voice_ok && face_ok)
                                    snprintf(buf, sizeof(buf), "ALLOW: Voice+Face OK (U%d)", best_user + 1);
                                else if (voice_ok && !face_ok)
                                    snprintf(buf, sizeof(buf), "Alarm: Face unknown (Voice U%d)", best_user + 1);
                                else if (!voice_ok && face_ok)
                                    snprintf(buf, sizeof(buf), "Alarm: Voice unknown (Face known)");
                                else
                                    snprintf(buf, sizeof(buf), "Alarm: Unknown person");
                            }
                            g_recognition_app->set_exec_text(buf);
                        }
                        // Report event for commands 1-5
                        if (pending >= 1 && pending <= 5) {
                            bool authorized = voice_ok && face_ok;
                            char jb[128];
                            snprintf(jb, sizeof(jb),
                                     "\"cmd_id\":%d,\"voice\":\"%s\",\"face\":\"%s\",\"result\":\"%s\"",
                                     pending,
                                     voice_ok ? "match" : "no_match",
                                     face_ok ? "match" : "no_match",
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
            ESP_LOGI(TAG, "Voice: VAD ON (energy=%.0f, noise=%.0f)",
                     frame_energy, g_voice_audio.noise_floor);
        } else if (g_voice_audio.silence_frames >= 10 && g_voice_audio.is_speaking) {
            g_voice_audio.is_speaking = false;
            ESP_LOGI(TAG, "Voice: VAD OFF");
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
                if (cmd_id == 6 && g_speaker_verifier && g_voice_cap_buf) {
                    g_voice_enrolling = true;
                    g_voice_verifying = false;
                    g_voice_collect_samples = 0;
                    // 2s post-detect delay captures "注册声纹，1,2,3" entirely in ring buffer
                    g_post_detect_samples = 16000 * 2; // capture follow-up speech after trigger
                    if (g_recognition_app) {
                        g_recognition_app->set_status_text("Enrolling voice... speak now");
                        g_recognition_app->set_exec_text("Recording...");
                    }
                } else if (cmd_id == 8) {
                    // Clear all voice enrollments
                    if (g_voice_db) g_voice_db->clear();
                    for (int i = 0; i < MAX_VOICE_USERS; i++) {
                        free(g_voice_embeddings[i]);
                        g_voice_embeddings[i] = nullptr;
                    }
                    g_voice_user_count = 0;
                    ESP_LOGI(TAG, "All voice enrollments cleared.");
                    if (g_recognition_app) {
                        g_recognition_app->set_status_text("Voices cleared");
                        g_recognition_app->set_exec_text("DB empty");
                    }
                } else if (cmd_id == 7 && g_speaker_verifier && g_voice_db) {
                    g_voice_verifying = true;
                    g_voice_enrolling = false;
                    g_voice_collect_samples = 0;
                    g_post_detect_samples = 16000 * 2; // capture follow-up speech after trigger
                    if (g_recognition_app) {
                        g_recognition_app->set_status_text("Verify voice... speak now");
                        g_recognition_app->set_exec_text("Recording...");
                    }
                } else if (g_voice_user_count > 0 && g_voice_cap_buf && g_speaker_verifier) {
                    // Commands 1-5, 7: immediate voice + face verification
                    if (g_recog_event_group && cmd_id <= 5) {
                        g_last_recog_face[0] = '\0';     // clear stale face result
                        g_skip_detect_count = 15;
                        xEventGroupSetBits(g_recog_event_group, 32);
                        g_face_triggered = true;          // wait for fresh face result
                    }
                    // Snapshot rolling buffer immediately (utterance already in buffer)
                    int available = (g_rolling_total >= ROLLING_BUF_SAMPLES) ? ROLLING_BUF_SAMPLES : g_rolling_total;
                    int max_samples = 16000 * 3;
                    int to_copy = (available < max_samples) ? available : max_samples;
                    int start = (g_rolling_wr - to_copy + ROLLING_BUF_SAMPLES) % ROLLING_BUF_SAMPLES;
                    for (int i = 0; i < to_copy; i++)
                        g_voice_cap_buf[i] = g_rolling_buf[(start + i) % ROLLING_BUF_SAMPLES];
                    g_voice_collect_samples = to_copy;
                    g_voice_verifying = true;
                    g_pending_cmd_id = cmd_id;
                    ESP_LOGI(TAG, "Voice verify snapshot for cmd %d, %d samples", cmd_id, to_copy);
                    if (g_recognition_app) {
                        g_recognition_app->set_status_text(cmd);
                        g_recognition_app->set_exec_text("Verifying voice...");
                    }
                } else {
                    // No voice users enrolled — fallback to face-only auth
                    if (g_recog_event_group) {
                        g_skip_detect_count = 15;
                        xEventGroupSetBits(g_recog_event_group, 32);
                    }
                    bool authorized = (strncmp(g_last_recog_face, "id:", 3) == 0);
                    if (g_recognition_app) {
                        g_recognition_app->set_status_text(cmd);
                        g_recognition_app->set_exec_text(authorized ? "Allow: Yes" : "Alarm!");
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

    // Light up backlight as power-on indicator (display content comes later via esp-who)
    gpio_set_direction(GPIO_NUM_20, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_20, 1);

    // ====================================================================
    // Phase 2: Init event loop (needed by esp-who components)
    // ====================================================================
    // esp_netif_init();  // C5 removed
    esp_event_loop_create_default();

    // ── Ethernet (IP101, RMII, pins 31/52/51) ──
    ESP_ERROR_CHECK(bsp_eth_init());
    ESP_LOGI(TAG, "Ethernet init successfully");

    // ── UART bridge to S3 (GPIO4/5, 921600 baud) ──
    uart_bridge_init();
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
        esp_codec_dev_set_in_gain(g_mic_handle, 40.0);
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
        bsp_display_cfg_t cfg = {
            .lvgl_port_cfg = lvgl_port_cfg,
            .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,  // full screen
            .double_buffer = 1,
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

        // Clock update timer
        lv_timer_create(on_clock_update_cb, 1000, g_phone);

        // Ethernet status bar + SNTP time sync
        static bool s_eth_sntp_started = false;
        lv_timer_create([](lv_timer_t *t) {
            auto *phone = (ESP_Brookesia_Phone *)t->user_data;
            esp_netif_t *eth = esp_netif_get_handle_from_ifkey("ETH");
            bool up = (eth && esp_netif_is_netif_up(eth));
            if (up) {
                phone->getHome().getStatusBar()->setWifiIconState(3); // reuse WiFi icon for ETH
                if (!s_eth_sntp_started) {
                    s_eth_sntp_started = true;
                    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
                    esp_sntp_setservername(0, (char *)"ntp.aliyun.com");
                    esp_sntp_setservername(1, (char *)"pool.ntp.org");
                    esp_sntp_init();
                    setenv("TZ", "CST-8", 1); tzset();
                    ESP_LOGI(TAG, "SNTP started over Ethernet");
                }
            } else {
                phone->getHome().getStatusBar()->setWifiIconState(0);
                s_eth_sntp_started = false;
            }
        }, 3000, g_phone);

        bsp_display_unlock();
    }

    // ---- Create Recognition App (pipeline only, UI created in FaceRecognitionApp::run()) ----
    auto recognition_app = new WhoRecognitionAppLCD(frame_cap);
    g_recognition_app = recognition_app;

    g_espdl_mutex = xSemaphoreCreateMutex();

    // ---- Speaker Verification Init ----
    {
        ESP_LOGI(TAG, "Init speaker verification...");
        g_speaker_verifier = new SpeakerVerification(3); // 3-second model (faster, EER=4.5%)
        g_embedding_dim = g_speaker_verifier->get_embedding_dim();
        g_voice_db = new dl::feat::FeatVerificationDatabase("/sdcard/voice.db", g_embedding_dim);
        if (!g_voice_db->is_valid()) {
            ESP_LOGW(TAG, "Voice DB init failed, creating new one...");
        }
        // Populate g_voice_embeddings[] from persistent DB
        g_voice_user_count = 0;
        if (g_voice_db->is_valid()) {
            auto labels = g_voice_db->get_labels();
            for (const auto &label : labels) {
                auto embeddings = g_voice_db->get_embeddings(label);
                if (!embeddings.empty() && g_voice_user_count < MAX_VOICE_USERS) {
                    int slot = g_voice_user_count;
                    if (!g_voice_embeddings[slot])
                        g_voice_embeddings[slot] = (float *)malloc(g_embedding_dim * sizeof(float));
                    if (g_voice_embeddings[slot])
                        memcpy(g_voice_embeddings[slot], embeddings[0].data(), g_embedding_dim * sizeof(float));
                    g_voice_user_count++;
                }
            }
            // One-time migration: delete old "user1" entries from single-user era
            if (g_voice_user_count > 0) {
                nvs_handle_t nvs;
                if (nvs_open("voice", NVS_READWRITE, &nvs) == ESP_OK) {
                    uint8_t migrated = 0;
                    nvs_get_u8(nvs, "migrated", &migrated);
                    if (!migrated) {
                        g_voice_db->clear();
                        for (int i = 0; i < MAX_VOICE_USERS; i++) { free(g_voice_embeddings[i]); g_voice_embeddings[i] = nullptr; }
                        g_voice_user_count = 0;
                        nvs_set_u8(nvs, "migrated", 1);
                        nvs_commit(nvs);
                        ESP_LOGI(TAG, "Voice DB migrated: old entries cleared, please re-enroll.");
                    }
                    nvs_close(nvs);
                }
            }
            ESP_LOGI(TAG, "Loaded %d voice user(s) from database", g_voice_user_count);
        }
        // Allocate rolling buffer (5s, 160KB) + capture buffer (3s, 96KB)
        g_rolling_buf = (int16_t *)heap_caps_malloc(ROLLING_BUF_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        g_voice_cap_buf = (int16_t *)heap_caps_malloc(16000 * 3 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!g_rolling_buf || !g_voice_cap_buf) {
            ESP_LOGE(TAG, "Failed to allocate voice buffers");
        }
        ESP_LOGI(TAG, "Speaker verification ready (emb=%d dims). Say 'zhu ce sheng wen' to enroll, 'shi bie sheng yin' to verify.",
                 g_embedding_dim);
    }

    // Heartbeat timer: report online every 60s
    esp_timer_handle_t hb_timer = nullptr;
    esp_timer_create_args_t hb_args = {
        .callback = [](void*) { report_event("heartbeat", "\"uptime\":0"); },
        .dispatch_method = ESP_TIMER_TASK,
        .name = "heartbeat"
    };
    esp_timer_create(&hb_args, &hb_timer);
    esp_timer_start_periodic(hb_timer, 60000000);  // 60s

    if (voice_params.multinet) {
        voice_task_params_t *p = (voice_task_params_t *)malloc(sizeof(voice_task_params_t));
        if (p) {
            *p = voice_params;
            xTaskCreatePinnedToCore(voice_recognition_task, "voice_cmd", 8192, p, 3, NULL, 0);
        } else {
            ESP_LOGE(TAG, "Failed to allocate voice task params");
        }
    }

    recognition_app->run();
}
