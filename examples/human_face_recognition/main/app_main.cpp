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
#include "esp_brookesia.hpp"
#include "who_lvgl_lcd.hpp"
#include "face_recognition_app.hpp"
#include "settings_app.hpp"
#include <cstring>

extern char g_last_recog_face[64];
extern char g_wifi_ip[32];

static const char *TAG = "app_main";
who::app::WhoRecognitionAppLCD *g_recognition_app = nullptr;
bool g_voice_paused = false;

// Brookesia globals
static ESP_Brookesia_Phone *g_phone = nullptr;
static lv_obj_t *g_phone_home_scr = nullptr;
lv_obj_t *g_camera_scr = nullptr;

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
    // Update brookesia status bar WiFi icon
    if (g_phone) {
        int level = done ? 3 : 0;  // 3=connected, 0=disconnected
        g_phone->getHome().getStatusBar()->setWifiIconState(level);
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
        if (g_voice_paused) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
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

    // ---- Initialize LVGL + Brookesia Phone UI ----
    // Set skip flag BEFORE creating recognition app (prevents WhoLCD from re-initializing LVGL)
    who::lcd::WhoLCD::s_skip_hw_init = true;

    // Initialize LVGL display (same config as WhoLCD::init for ESP32P4)
    {
        lvgl_port_cfg_t lvgl_port_cfg = {
            .task_priority = 5,
            .task_stack = 8192,
            .task_affinity = -1,
            .task_max_sleep_ms = 500,
            .timer_period_ms = 5,
        };
        bsp_display_cfg_t cfg = {
            .lvgl_port_cfg = lvgl_port_cfg,
            .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,  // full screen for FULL render mode
            .double_buffer = 1,  // enable double buffering for async DMA flush
            .hw_cfg = {
                .dsi_bus = {
                    .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
                    .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
                }
            },
            .flags = {
                .buff_dma = true,
                .buff_spiram = true,  // full-screen buffers need PSRAM
                .sw_rotate = true,
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

        // Save desktop screen BEFORE creating camera UI on a dedicated screen
        lv_obj_t *home_scr = lv_screen_active();
        g_phone_home_scr = home_scr;

        // Create a dedicated screen for camera and load it temporarily
        // so WhoRecognitionAppLCD creates everything on it (not on desktop)
        g_camera_scr = lv_obj_create(NULL);
        lv_screen_load(g_camera_scr);

        // Add Exit button (bottom-right, next to WiFi) to return to desktop
        {
            lv_obj_t *btn_exit = lv_button_create(g_camera_scr);
            lv_obj_t *label = lv_label_create(btn_exit);
            lv_label_set_text(label, "Exit");
            lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
            lv_obj_center(label);
            lv_obj_align(btn_exit, LV_ALIGN_TOP_RIGHT, -10, 300);
            lv_obj_set_style_bg_color(btn_exit, lv_color_hex(0x662222), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(btn_exit, LV_OPA_80, LV_PART_MAIN);
            lv_obj_set_style_border_width(btn_exit, 2, LV_PART_MAIN);
            lv_obj_set_style_border_color(btn_exit, lv_color_hex(0xcc4444), LV_PART_MAIN);
            lv_obj_set_style_radius(btn_exit, 6, LV_PART_MAIN);
            lv_obj_add_event_cb(btn_exit, [](lv_event_t *e) {
                if (g_phone_home_scr) lv_screen_load(g_phone_home_scr);
            }, LV_EVENT_CLICKED, nullptr);
        }

        bsp_display_unlock();
    }

    // ---- Create Recognition App (on camera_scr, not desktop) ----
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

    // Switch back to desktop — camera runs on background screen
    // Camera preview will show when user clicks Camera icon (Phase 2.2)
    if (g_phone_home_scr) lv_screen_load(g_phone_home_scr);

    recognition_app->run();
}
