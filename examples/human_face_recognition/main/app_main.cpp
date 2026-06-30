#include "frame_cap_pipeline.hpp"
#include "who_recognition_app_lcd.hpp"
#include "who_recognition_app_term.hpp"
#include "who_spiflash_fatfs.hpp"
#include "wifi_provisioning.hpp"
#include "event_reporter.hpp"
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
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include <cstring>

extern char g_last_recog_face[64];
extern char g_wifi_ip[32];

static const char *TAG = "app_main";
static who::app::WhoRecognitionAppLCD *g_recognition_app = nullptr;

// 配网状态 — 供 esp-who display 初始化后读取
static char g_wifi_status[128] = "Starting...";

// --- 配网状态回调 ---
static void wifi_prov_status_cb(const char *status, bool done)
{
    strncpy(g_wifi_status, status, sizeof(g_wifi_status) - 1);
    ESP_LOGI(TAG, "WiFi: %s", status);
    if (g_recognition_app) {
        g_recognition_app->set_wifi_text(status);
    }
    (void)done;
}

using namespace who::frame_cap;
using namespace who::app;

EventGroupHandle_t g_recog_event_group = nullptr;
static esp_codec_dev_handle_t g_mic_handle = nullptr;
static SemaphoreHandle_t g_espdl_mutex = nullptr;
static volatile int g_skip_detect_count = 0;

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

    ESP_LOGI(TAG, "Voice ready: chunksize=%d, listening...", chunksize);

    int16_t *buffer = (int16_t *)malloc(chunksize * sizeof(int16_t));
    int64_t last_log = 0;
    int read_count = 0;
    while (g_mic_handle && buffer) {
        esp_codec_dev_read(g_mic_handle, buffer, chunksize * sizeof(int16_t));
        read_count++;
        int64_t now = esp_timer_get_time();
        if (now - last_log > 1000000) {
            int nz = 0;
            for (int i = 0; i < chunksize; i++) if (buffer[i] != 0) nz++;
            ESP_LOGI(TAG, "Codec read: %d calls/sec, nonzero=%d/%d", read_count, nz, chunksize);
            read_count = 0;
            last_log = now;
        }
        if (g_skip_detect_count > 0) {
            g_skip_detect_count = g_skip_detect_count - 1;
        } else if (xSemaphoreTake(g_espdl_mutex, pdMS_TO_TICKS(100))) {
            esp_mn_state_t state = multinet->detect(mn_data, buffer);
            if (state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *r = multinet->get_results(mn_data);
                const char *cmd = "Cmd: Unknown";
                switch (r->command_id[0]) {
                    case 1: cmd = "Cmd: Open TV"; break;
                    case 2: cmd = "Cmd: Close TV"; break;
                    case 3: cmd = "Cmd: Open Door"; break;
                    case 4: cmd = "Cmd: Close Door"; break;
                    case 5: cmd = "Cmd: Identify"; break;
                }
                ESP_LOGI(TAG, "Detected: %s", cmd);

                // Every voice command triggers face recognition
                if (g_recog_event_group) {
                    g_skip_detect_count = 100;
                    xEventGroupSetBits(g_recog_event_group, 32);
                }

                // Wait briefly for recognition to complete
                vTaskDelay(pdMS_TO_TICKS(1500));

                bool authorized = (strncmp(g_last_recog_face, "id:", 3) == 0);
                const char *who = authorized ? g_last_recog_face : "stranger";
                const char *result = authorized ? "allow" : "alarm";

                if (g_recognition_app) {
                    g_recognition_app->set_status_text(cmd);
                    if (authorized) {
                        g_recognition_app->set_exec_text("Allow: Yes");
                    } else {
                        g_recognition_app->set_exec_text("Alarm!");
                    }
                }

                // Combined event: who + what command + result
                {
                    char json_buf[128];
                    snprintf(json_buf, sizeof(json_buf),
                             "\"cmd\":\"%s\",\"face\":\"%s\",\"result\":\"%s\"",
                             cmd, who, result);
                    report_event("voice_command", json_buf);
                }
            }
            xSemaphoreGive(g_espdl_mutex);
        }
        taskYIELD();
    }
    free(buffer);
    vTaskDelete(NULL);
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
    esp_netif_init();
    esp_event_loop_create_default();

    // ====================================================================
    // Phase 3: Filesystem + Camera + Audio + Recognition (WiFi later on click)
    // ====================================================================
#if CONFIG_DB_FATFS_FLASH
    ESP_ERROR_CHECK(fatfs_flash_mount());
#elif CONFIG_DB_SPIFFS
    ESP_ERROR_CHECK(bsp_spiffs_mount());
#endif
#if CONFIG_DB_FATFS_SDCARD || CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD || CONFIG_HUMAN_FACE_FEAT_MODEL_IN_SDCARD
    ESP_ERROR_CHECK(bsp_sdcard_mount());
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
        g_mic_handle = bsp_audio_codec_microphone_init();

        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16, .channel = 2, .channel_mask = 1,
            .sample_rate = 16000, .mclk_multiple = 256,
        };
        esp_codec_dev_open(speaker, &fs);
        esp_codec_dev_open(g_mic_handle, &fs);
        esp_codec_dev_set_in_gain(g_mic_handle, 40.0);
        ESP_LOGI(TAG, "Audio: BSP init complete");
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
                voice_params.multinet->set_det_threshold(voice_params.mn_data, 0.10);
                esp_mn_commands_add(1, "da kai dian shi");
                esp_mn_commands_add(2, "guan bi dian shi");
                esp_mn_commands_add(3, "da kai men");
                esp_mn_commands_add(4, "guan bi men");
                esp_mn_commands_add(5, "shi bie ren lian");
                esp_mn_commands_update();
                voice_params.chunksize = voice_params.multinet->get_samp_chunksize(voice_params.mn_data);
            }
        }
        vTaskPrioritySet(NULL, 5);
    }

    // ---- Create Recognition App (esp-who initializes display internally) ----
    auto recognition_app = new WhoRecognitionAppLCD(frame_cap);
    g_recognition_app = recognition_app;

    // WiFi button: click to start provisioning
    extern void (*g_on_wifi_btn_click)();
    g_on_wifi_btn_click = []() {
        wifi_provisioning_start_async(wifi_prov_status_cb);
    };
    recognition_app->set_wifi_text("WiFi: Off");

    g_espdl_mutex = xSemaphoreCreateMutex();

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
        *p = voice_params;
        xTaskCreatePinnedToCore(voice_recognition_task, "voice_cmd", 8192, p, 2, NULL, 0);
    }

    recognition_app->run();
}
