/**
 * @file xiaozhi_app.cpp
 * @brief XiaoZhi voice assistant — brookesia phone app implementation.
 */

#include "xiaozhi_app.hpp"
#include "xiaozhi_audio_bridge.hpp"
#include "xiaozhi_emoji.hpp"
#include "uart_bridge.hpp"
#include "xiaozhi/uart_frame_protocol.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <cstring>
#include <cstdio>

static const char *TAG = "XiaoZhiApp";

// External globals from app_main.cpp
extern bool g_voice_paused;
extern void *g_recognition_app;  // WhoRecognitionAppLCD*

/* ── Message queue item ────────────────────── */
typedef struct {
    char type[16];
    char text[256];
} xz_msg_t;

/* ── Static pointer for UART callback access ── */
static XiaoZhiApp *s_instance = nullptr;

/* ── UART JSON callback (Core 0, uart_rx_task) ── */
static void on_uart_json(const char *line)
{
    // Filter for xiaozhi JSON messages: {"dev":"s3","xz":{...}}
    if (!strstr(line, "\"xz\":")) return;
    if (s_instance) s_instance->on_xiaozhi_json(line);
}

/* ───────────────────────────────────────────────
 *  XiaoZhiApp implementation
 * ─────────────────────────────────────────────── */

XiaoZhiApp::XiaoZhiApp()
    : ESP_Brookesia_PhoneApp(
          {/* core_data */
           .name = "XiaoZhi",
           .launcher_icon = ESP_BROOKESIA_STYLE_IMAGE(
               &esp_brookesia_image_large_app_launcher_default_112_112),
           .screen_size = ESP_BROOKESIA_STYLE_SIZE_RECT_PERCENT(100, 100),
           .flags = {.enable_default_screen = 1,
                     .enable_recycle_resource = 1,
                     .enable_resize_visual_area = 1}},
          {/* phone_data */
           .app_launcher_page_index = 0,
           .status_bar_visual_mode =
               ESP_BROOKESIA_STATUS_BAR_VISUAL_MODE_SHOW_FIXED,
           .navigation_bar_visual_mode =
               ESP_BROOKESIA_NAVIGATION_BAR_VISUAL_MODE_HIDE,
           .flags = {.enable_navigation_gesture = 1}})
{
}

XiaoZhiApp::~XiaoZhiApp()
{
    if (m_msg_queue) {
        vQueueDelete(m_msg_queue);
        m_msg_queue = nullptr;
    }
}

bool XiaoZhiApp::run()
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a1a), LV_PART_MAIN);

    /* ── Title ──────────────────────────────── */
    m_title_label = lv_label_create(scr);
    lv_label_set_text(m_title_label, "XiaoZhi AI");
    lv_obj_set_style_text_color(m_title_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_title_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(m_title_label, LV_ALIGN_TOP_MID, 0, 10);

    extern const lv_font_t lv_font_simsun_16_cjk;

    /* ── Status line ────────────────────────── */
    m_status_label = lv_label_create(scr);
    lv_label_set_text(m_status_label, "Connecting...");
    lv_obj_set_style_text_color(m_status_label, lv_color_hex(0x88AAFF), LV_PART_MAIN);
    lv_obj_align(m_status_label, LV_ALIGN_TOP_MID, 0, 40);

    /* ── Emoji image (centered, scaled to fit) ── */
    m_emoji_img = lv_image_create(scr);
    lv_obj_set_style_bg_opa(m_emoji_img, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_align(m_emoji_img, LV_ALIGN_CENTER, 0, 20);

    // Preload all 4 emoji images from SD card
    xz_emoji_init();

    // Set initial neutral emoji
    const lv_image_dsc_t *init_dsc = xz_emoji_get("neutral");
    if (init_dsc) {
        lv_image_set_src(m_emoji_img, init_dsc);
        float sf = 280.0f / (float)init_dsc->header.h;  // scale to 280px height
        lv_image_set_scale(m_emoji_img, (uint16_t)(sf * 256.0f));
    }

    /* ── TTS area (xiaozhi reply) ──────────── */
    m_tts_label = lv_label_create(scr);
    lv_label_set_text(m_tts_label, "");
    lv_obj_set_style_text_color(m_tts_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_tts_label, &lv_font_simsun_16_cjk, LV_PART_MAIN);
    lv_obj_set_width(m_tts_label, lv_pct(90));
    lv_label_set_long_mode(m_tts_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(m_tts_label, LV_ALIGN_BOTTOM_MID, 0, -60);

    /* ── Emotion text label ─────────────────── */
    m_emotion_label = lv_label_create(scr);
    lv_label_set_text(m_emotion_label, "");
    lv_obj_set_style_text_color(m_emotion_label, lv_color_hex(0xFFAA44), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_emotion_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(m_emotion_label, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* ── Exit button ────────────────────────── */
    m_exit_btn = lv_button_create(scr);
    lv_obj_set_size(m_exit_btn, 160, 50);
    lv_obj_align(m_exit_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_set_style_bg_color(m_exit_btn, lv_color_hex(0x603030), LV_PART_MAIN);

    lv_obj_t *btn_label = lv_label_create(m_exit_btn);
    lv_label_set_text(btn_label, "Exit");
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0xFFCCCC), LV_PART_MAIN);
    lv_obj_center(btn_label);

    lv_obj_add_event_cb(m_exit_btn, [](lv_event_t *e) {
        auto *app = (XiaoZhiApp *)lv_event_get_user_data(e);
        if (app) app->notifyCoreClosed();
    }, LV_EVENT_CLICKED, this);

    /* ── Message queue ──────────────────────── */
    m_msg_queue = xQueueCreate(32, sizeof(xz_msg_t));
    if (!m_msg_queue) {
        ESP_LOGE(TAG, "Failed to create message queue");
    }

    /* ── Register UART JSON callback ────────── */
    uart_bridge_on_json(on_uart_json);
    m_callback_registered = true;
    s_instance = this;

    /* ── Start audio bridge ─────────────────── */
    // Pause voice recognition while in xiaozhi mode
    g_voice_paused = true;

    // Tell S3 to enter xiaozhi mode
    uint8_t f[4] = {UART_FRAME_CTRL, CTRL_ENTER_XIAOZHI, 0, 0};
    uart_bridge_send_frame(UART_FRAME_CTRL, f, 4);

    // Start mic → UART pipeline
    xiaozhi_audio_bridge_start();

    /* ── LVGL timer for UI updates (Core 1) ─── */
    m_timer = lv_timer_create(timer_cb, 150, this);

    ESP_LOGI(TAG, "XiaoZhi app started");
    return true;
}

bool XiaoZhiApp::back()
{
    notifyCoreClosed();
    return true;
}

bool XiaoZhiApp::close()
{
    ESP_LOGI(TAG, "XiaoZhi app closing...");

    /* ── Free emoji images ───────────────────── */
    xz_emoji_deinit();

    /* ── Stop LVGL timer ────────────────────── */
    if (m_timer) {
        lv_timer_delete(m_timer);
        m_timer = nullptr;
    }

    /* ── Stop audio bridge ──────────────────── */
    xiaozhi_audio_bridge_stop();

    /* ── Tell S3 to exit xiaozhi mode ───────── */
    uint8_t f[4] = {UART_FRAME_CTRL, CTRL_EXIT_XIAOZHI, 0, 0};
    uart_bridge_send_frame(UART_FRAME_CTRL, f, 4);

    /* ── Resume voice recognition ───────────── */
    g_voice_paused = false;

    /* ── Unregister UART callback ───────────── */
    // No explicit unregister API — the callback checks s_instance
    s_instance = nullptr;
    m_callback_registered = false;

    /* ── Drain message queue ────────────────── */
    xz_msg_t dummy;
    while (xQueueReceive(m_msg_queue, &dummy, 0) == pdTRUE) {}

    ESP_LOGI(TAG, "XiaoZhi app closed");
    return true;
}

/* ── Thread-safe JSON push (Core 0 → Core 1) ─ */

void XiaoZhiApp::on_xiaozhi_json(const char *json_line)
{
    if (!m_msg_queue) return;

    // Quick parse: extract "type" and "text"/"emotion" fields
    xz_msg_t msg = {};
    const char *p;

    p = strstr(json_line, "\"type\":\"");
    if (p) {
        p += 8;
        size_t n = 0;
        while (*p && *p != '"' && n < sizeof(msg.type) - 1)
            msg.type[n++] = *p++;
        msg.type[n] = 0;
    }

    // Try "text" field (for tts/stt)
    p = strstr(json_line, "\"text\":\"");
    if (p) {
        p += 8;
        size_t n = 0;
        while (*p && *p != '"' && n < sizeof(msg.text) - 1) {
            if (*p == '\\' && *(p+1) == '"') { p++; continue; }
            msg.text[n++] = *p++;
        }
        msg.text[n] = 0;
    }

    // Try "emotion" field
    if (msg.text[0] == 0) {
        p = strstr(json_line, "\"emotion\":\"");
        if (p) {
            p += 11;
            size_t n = 0;
            while (*p && *p != '"' && n < sizeof(msg.text) - 1)
                msg.text[n++] = *p++;
            msg.text[n] = 0;
        }
    }

    xQueueSend(m_msg_queue, &msg, 0);  // non-blocking
}

/* ── LVGL timer callback ───────────────────── */

void XiaoZhiApp::timer_cb(lv_timer_t *timer)
{
    auto *app = (XiaoZhiApp *)timer->user_data;
    if (app) app->process_messages();
}

void XiaoZhiApp::process_messages()
{
    xz_msg_t msg;
    int msg_count = 0;
    while (xQueueReceive(m_msg_queue, &msg, 0) == pdTRUE && msg_count++ < 8) {
        if (strcmp(msg.type, "stt") == 0) {
            lv_label_set_text(m_status_label, "Listening...");
        } else if (strcmp(msg.type, "tts") == 0) {
            // Show only latest xiaozhi reply (replace, not accumulate)
            lv_label_set_text(m_tts_label, msg.text);
            lv_label_set_text(m_status_label, "Speaking...");
        } else if (strcmp(msg.type, "emotion") == 0) {
            ESP_LOGI(TAG, "emoji switch: '%s'", msg.text);
            const lv_image_dsc_t *dsc = xz_emoji_get(msg.text);
            if (dsc && m_emoji_img) {
                lv_image_set_src(m_emoji_img, dsc);
                float sf = 280.0f / (float)dsc->header.h;
                lv_image_set_scale(m_emoji_img, (uint16_t)(sf * 256.0f));
                ESP_LOGI(TAG, "emoji set: %dx%d scale=%.2f",
                         (int)dsc->header.w, (int)dsc->header.h, (double)sf);
            } else {
                ESP_LOGW(TAG, "emoji failed: dsc=%p img=%p", (void*)dsc, (void*)m_emoji_img);
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%.63s", msg.text);
            lv_label_set_text(m_emotion_label, buf);
        } else if (strcmp(msg.type, "mcp") == 0) {
            lv_label_set_text(m_status_label, "MCP active");
        }
    }

    // If audio bridge is not active, show disconnected
    if (!xiaozhi_audio_bridge_is_active()) {
        lv_label_set_text(m_status_label, "Disconnected");
    }
}
