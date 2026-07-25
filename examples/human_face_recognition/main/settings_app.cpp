#include "settings_app.hpp"
#include "uart_bridge.hpp"
#include "xiaozhi/uart_frame_protocol.h"
// #include "wifi_provisioning.hpp"  // C5 removed

// ── Stub WiFi globals (was in wifi_provisioning.cpp, C5 removed) ──
static char g_wifi_ip[32] = {0};
static char g_wifi_ssid[33] = {0};
static bool wifi_is_connected() { return false; }
static void wifi_provisioning_start_async(void (*)(const char*, bool)) {}

// wallpaper loaded from SD card — no compiled-in default
#include "who_recognition_app_lcd.hpp"
#include "who_recognition.hpp"
#include "human_face_recognition.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include <dirent.h>
#include <sys/stat.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

static const char *TAG = "SettingsApp";
extern who::app::WhoRecognitionAppLCD *g_recognition_app;
extern lv_image_dsc_t *g_active_wp_dsc;
extern void *g_active_wp_data;
LV_FONT_DECLARE(montserrat_bold_26);
LV_FONT_DECLARE(montserrat_bold_20);

// Static members for cross-task WiFi status
char SettingsApp::s_wifi_status_buf[256] = {0};
bool SettingsApp::s_wifi_status_dirty = false;
bool SettingsApp::s_wifi_status_done = false;

// Static WiFi status callback (runs in provisioning task)
void SettingsApp::wifi_status_cb(const char *status, bool done) {
    strncpy(s_wifi_status_buf, status, sizeof(s_wifi_status_buf) - 1);
    s_wifi_status_dirty = true;
    s_wifi_status_done = done;
}

// ============================================================
// Constructor
// ============================================================
SettingsApp::SettingsApp()
    : ESP_Brookesia_PhoneApp(
          {/* core_data */
           .name = "Settings",
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

SettingsApp::~SettingsApp() = default;

// ============================================================
// Lifecycle
// ============================================================
bool SettingsApp::run()
{
    ESP_LOGI(TAG, "Settings App started");
    create_main_page();
    return true;
}

// ============================================================
// Helper: create a styled menu row panel
// ============================================================
static lv_obj_t *menu_row_create(lv_obj_t *parent, const char *text, int y)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, lv_pct(90), 60);
    lv_obj_align(p, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x2a2a3e), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(p, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(p, lv_color_hex(0x555577), LV_PART_MAIN);
    lv_obj_set_style_radius(p, 8, LV_PART_MAIN);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *label = lv_label_create(p);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &montserrat_bold_26, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 20, 0);

    lv_obj_t *arrow = lv_label_create(p);
    lv_label_set_text(arrow, ">");
    lv_obj_set_style_text_color(arrow, lv_color_hex(0x8888AA), LV_PART_MAIN);
    lv_obj_set_style_text_font(arrow, &montserrat_bold_26, LV_PART_MAIN);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -20, 0);

    return p;
}

// ============================================================
// Page management helpers
// ============================================================
lv_obj_t *SettingsApp::create_page_container()
{
    lv_obj_t *c = lv_obj_create(lv_scr_act());
    lv_obj_set_size(c, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(c, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c, 0, LV_PART_MAIN);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    return c;
}

void SettingsApp::show_page(lv_obj_t *page, bool push_to_stack)
{
    if (m_current_page && push_to_stack) {
        m_page_stack.push_back(m_current_page);
    }
    if (m_current_page) {
        lv_obj_add_flag(m_current_page, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
    m_current_page = page;
}

// ============================================================
// Main Page
// ============================================================
void SettingsApp::create_main_page()
{
    m_main_page = create_page_container();
    show_page(m_main_page, false);  // first page, no stack push

    lv_obj_t *title = lv_label_create(m_main_page);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &montserrat_bold_26, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    auto *btn1 = menu_row_create(m_main_page, "Face Database", 80);
    lv_obj_add_event_cb(btn1, [](lv_event_t *e) {
        ((SettingsApp *)lv_event_get_user_data(e))->create_face_page();
    }, LV_EVENT_CLICKED, this);

    auto *btn2 = menu_row_create(m_main_page, "WiFi", 155);
    lv_obj_add_event_cb(btn2, [](lv_event_t *e) {
        ((SettingsApp *)lv_event_get_user_data(e))->create_wifi_page();
    }, LV_EVENT_CLICKED, this);

    auto *btn3 = menu_row_create(m_main_page, "Wallpaper", 230);
    lv_obj_add_event_cb(btn3, [](lv_event_t *e) {
        ((SettingsApp *)lv_event_get_user_data(e))->create_wallpaper_page();
    }, LV_EVENT_CLICKED, this);

    auto *btn4 = menu_row_create(m_main_page, "About Device", 305);
    lv_obj_add_event_cb(btn4, [](lv_event_t *e) {
        ((SettingsApp *)lv_event_get_user_data(e))->create_about_page();
    }, LV_EVENT_CLICKED, this);

    auto *btn5 = menu_row_create(m_main_page, "Radar Training", 380);
    lv_obj_add_event_cb(btn5, [](lv_event_t *e) {
        ((SettingsApp *)lv_event_get_user_data(e))->create_radar_page();
    }, LV_EVENT_CLICKED, this);

    // Reset Voiceprints — button because voice cmd 8 ("qing chu sheng wen")
    // is a MultiNet blind spot and never gets recognized.
    extern int voice_db_reset(void);
    auto *btn6 = menu_row_create(m_main_page, "Reset Voiceprints", 420);
    lv_obj_add_event_cb(btn6, [](lv_event_t *e) {
        static bool awaiting = false;
        static int64_t first_tap_us = 0;
        lv_obj_t *btn = lv_event_get_target_obj(e);
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        int64_t now = esp_timer_get_time();
        if (!awaiting || (now - first_tap_us) > 3000000) {
            // First tap (or previous confirm expired after 3s)
            awaiting = true;
            first_tap_us = now;
            lv_label_set_text(label, "Tap again to reset");
        } else {
            awaiting = false;
            int n = voice_db_reset();
            char buf[48];
            snprintf(buf, sizeof(buf), "Reset done (%d cleared)", n);
            lv_label_set_text(label, buf);
        }
    }, LV_EVENT_CLICKED, this);
}

// ============================================================
// Back navigation
// ============================================================
bool SettingsApp::back()
{
    if (!m_page_stack.empty()) {
        lv_obj_t *prev = m_page_stack.back();
        m_page_stack.pop_back();
        show_page(prev, false);  // don't push back to stack
        return true;
    }
    notifyCoreClosed();
    return true;
}

bool SettingsApp::close()
{
    ESP_LOGI(TAG, "Settings closed");
    // Stop WiFi status timer if active
    if (m_wifi_status_timer) {
        lv_timer_del(m_wifi_status_timer);
        m_wifi_status_timer = nullptr;
    }
    // brookesia cleans up the screen and all containers via enable_recycle_resource=1
    return true;
}

// ============================================================
// Face Database Page
// ============================================================
void SettingsApp::refresh_face_list()
{
    if (!m_face_list) return;

    // Clear old content
    lv_obj_clean(m_face_list);

    if (!g_recognition_app) {
        lv_label_set_text(m_face_count_label, "Camera app not available");
        return;
    }

    auto *recog = g_recognition_app->get_recognition()->get_recognition_task();
    auto *fb = recog->get_recognizer();
    if (!fb) {
        lv_label_set_text(m_face_count_label, "Recognizer not initialized");
        return;
    }

    int num = fb->get_num_feats();
    char buf[64];
    snprintf(buf, sizeof(buf), "Enrolled: %d faces", num);
    lv_label_set_text(m_face_count_label, buf);

    // get_feat_ids() removed in newer esp-dl; enumerate numerically
    for (int id = 0; id < num; id++) {
        lv_obj_t *row = lv_obj_create(m_face_list);
        lv_obj_set_size(row, lv_pct(95), 45);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2a2a3e), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x444466), 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 10, 0);
        lv_obj_set_style_pad_right(row, 10, 0);

        snprintf(buf, sizeof(buf), "Face ID: %d", id);
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl, &montserrat_bold_20, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *btn = lv_button_create(row);
        lv_obj_set_size(btn, 70, 32);
        lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x662222), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xcc4444), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_t *btxt = lv_label_create(btn);
        lv_label_set_text(btxt, "Del");
        lv_obj_center(btxt);

        // Store SettingsApp* in row, face_id in btn
        lv_obj_set_user_data(row, this);
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            // Get the row parent to access stored SettingsApp*
            lv_obj_t *row = lv_obj_get_parent(lv_event_get_target_obj(e));
            auto *self = (SettingsApp *)lv_obj_get_user_data(row);
            // Get face_id from the label text
            lv_obj_t *label = lv_obj_get_child(row, 0);
            const char *txt = lv_label_get_text(label);
            int fid = 0;
            if (txt && sscanf(txt, "Face ID: %d", &fid) == 1) {
                if (g_recognition_app) {
                    auto *r = g_recognition_app->get_recognition()->get_recognition_task();
                    auto *f = r->get_recognizer();
                    if (f) f->delete_feat((uint16_t)fid);
                }
                self->refresh_face_list();
            }
        }, LV_EVENT_CLICKED, nullptr);
    }
}

void SettingsApp::create_face_page()
{
    if (m_face_page) { show_page(m_face_page); return; }
    m_face_page = create_page_container();
    show_page(m_face_page);

    lv_obj_t *title = lv_label_create(m_face_page);
    lv_label_set_text(title, "Face Database");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &montserrat_bold_26, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    m_face_count_label = lv_label_create(m_face_page);
    lv_obj_set_style_text_color(m_face_count_label, lv_color_hex(0xAAAAFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_face_count_label, &montserrat_bold_20, LV_PART_MAIN);
    lv_obj_align(m_face_count_label, LV_ALIGN_TOP_LEFT, 20, 60);

    // Clear All button — two-click confirmation to prevent accidental data loss
    lv_obj_t *btn_all = lv_button_create(m_face_page);
    lv_obj_set_size(btn_all, 140, 36);
    lv_obj_align(btn_all, LV_ALIGN_TOP_RIGHT, -20, 55);
    lv_obj_set_style_bg_color(btn_all, lv_color_hex(0x662222), 0);
    lv_obj_set_style_border_width(btn_all, 1, 0);
    lv_obj_set_style_border_color(btn_all, lv_color_hex(0xcc4444), 0);
    lv_obj_set_style_radius(btn_all, 6, 0);
    lv_obj_t *atxt = lv_label_create(btn_all);
    lv_label_set_text(atxt, "Clear All");
    lv_obj_center(atxt);

    // Two-click confirmation state
    struct ClearAllState {
        bool awaiting_confirm = false;
        lv_timer_t *reset_timer = nullptr;
        lv_obj_t *btn = nullptr;
        lv_obj_t *label = nullptr;
    };
    auto *clear_state = new ClearAllState();
    clear_state->btn = btn_all;
    clear_state->label = atxt;

    lv_obj_add_event_cb(btn_all, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        auto *state = (ClearAllState *)lv_obj_get_user_data(lv_event_get_target_obj(e));

        if (!state->awaiting_confirm) {
            // First click: ask for confirmation
            state->awaiting_confirm = true;
            lv_label_set_text(state->label, "Sure?");
            lv_obj_set_style_bg_color(state->btn, lv_color_hex(0xcc2222), 0);
            // Auto-reset after 3 seconds
            state->reset_timer = lv_timer_create([](lv_timer_t *t) {
                auto *s = (ClearAllState *)t->user_data;
                s->awaiting_confirm = false;
                s->reset_timer = nullptr;
                // Reset button appearance
                lv_label_set_text(s->label, "Clear All");
                lv_obj_set_style_bg_color(s->btn, lv_color_hex(0x662222), 0);
                lv_timer_del(t);
            }, 3000, state);
        } else {
            // Second click: execute!
            if (state->reset_timer) {
                lv_timer_del(state->reset_timer);
                state->reset_timer = nullptr;
            }
            state->awaiting_confirm = false;
            if (g_recognition_app) {
                auto *r = g_recognition_app->get_recognition()->get_recognition_task();
                auto *f = r->get_recognizer();
                if (f) f->clear_all_feats();
            }
            self->refresh_face_list();
            // Reset button appearance
            lv_label_set_text(state->label, "Clear All");
            lv_obj_set_style_bg_color(state->btn, lv_color_hex(0x662222), 0);
        }
    }, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btn_all, clear_state);

    // Clean up ClearAllState when button is deleted (page exit)
    lv_obj_add_event_cb(btn_all, [](lv_event_t *e) {
        auto *state = (ClearAllState *)lv_obj_get_user_data(lv_event_get_target_obj(e));
        if (state) {
            if (state->reset_timer) lv_timer_del(state->reset_timer);
            delete state;
        }
    }, LV_EVENT_DELETE, nullptr);

    // Scrollable face list
    m_face_list = lv_obj_create(m_face_page);
    lv_obj_set_size(m_face_list, lv_pct(90), lv_pct(60));
    lv_obj_align(m_face_list, LV_ALIGN_TOP_MID, 0, 105);
    lv_obj_set_style_bg_opa(m_face_list, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(m_face_list, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(m_face_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_face_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(m_face_list, 4, LV_PART_MAIN);
    lv_obj_add_flag(m_face_list, LV_OBJ_FLAG_SCROLLABLE);

    // Back button — shared navigation logic
    lv_obj_t *btn_back = lv_button_create(m_face_page);
    lv_obj_set_size(btn_back, 80, 36);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x444444), 0);
    lv_obj_t *btxt = lv_label_create(btn_back);
    lv_label_set_text(btxt, "Back");
    lv_obj_center(btxt);
    lv_obj_add_event_cb(btn_back, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        self->back();
    }, LV_EVENT_CLICKED, this);

    refresh_face_list();
}

// ============================================================
// WiFi Page — full connect/disconnect with real-time status
// ============================================================
void SettingsApp::create_wifi_page()
{
    if (m_wifi_page) { show_page(m_wifi_page); return; }
    m_wifi_page = create_page_container();
    show_page(m_wifi_page);

    lv_obj_t *title = lv_label_create(m_wifi_page);
    lv_label_set_text(title, "WiFi");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &montserrat_bold_26, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Status label
    m_wifi_status_label = lv_label_create(m_wifi_page);
    lv_obj_set_style_text_color(m_wifi_status_label, lv_color_hex(0xAAAAFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_wifi_status_label, &montserrat_bold_20, LV_PART_MAIN);
    lv_obj_align(m_wifi_status_label, LV_ALIGN_TOP_LEFT, 20, 80);

    // SSID label
    m_wifi_ssid_label = lv_label_create(m_wifi_page);
    lv_obj_set_style_text_color(m_wifi_ssid_label, lv_color_hex(0x88CC88), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_wifi_ssid_label, &montserrat_bold_20, LV_PART_MAIN);
    lv_obj_align(m_wifi_ssid_label, LV_ALIGN_TOP_LEFT, 20, 110);

    // IP label
    m_wifi_ip_label = lv_label_create(m_wifi_page);
    lv_obj_set_style_text_color(m_wifi_ip_label, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_wifi_ip_label, &montserrat_bold_20, LV_PART_MAIN);
    lv_obj_align(m_wifi_ip_label, LV_ALIGN_TOP_LEFT, 20, 140);

    // Update initial status
    char buf[128];
    if (g_wifi_ssid[0]) {
        snprintf(buf, sizeof(buf), "Connected to:");
        lv_label_set_text(m_wifi_status_label, buf);
        snprintf(buf, sizeof(buf), "SSID: %s", g_wifi_ssid);
        lv_label_set_text(m_wifi_ssid_label, buf);
        snprintf(buf, sizeof(buf), "IP: %s", g_wifi_ip[0] ? (g_wifi_ip + 6) : "N/A");
        lv_label_set_text(m_wifi_ip_label, buf);
    } else {
        lv_label_set_text(m_wifi_status_label, "WiFi: Not connected");
        lv_label_set_text(m_wifi_ssid_label, "");
        lv_label_set_text(m_wifi_ip_label, "IP: N/A");
    }

    // Connect / Reconnect button
    // Spinner label (shown during provisioning)
    m_wifi_spinner = lv_label_create(m_wifi_page);
    lv_label_set_text(m_wifi_spinner, "...");
    lv_obj_set_style_text_color(m_wifi_spinner, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_wifi_spinner, &montserrat_bold_26, LV_PART_MAIN);
    lv_obj_align(m_wifi_spinner, LV_ALIGN_TOP_MID, 0, 215);
    lv_obj_add_flag(m_wifi_spinner, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *btn_connect = lv_button_create(m_wifi_page);
    lv_obj_set_size(btn_connect, 200, 50);
    lv_obj_align(btn_connect, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_set_style_bg_color(btn_connect, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_bg_opa(btn_connect, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_connect, 8, 0);
    lv_obj_t *cbtxt = lv_label_create(btn_connect);
    if (g_wifi_ssid[0]) {
        lv_label_set_text(cbtxt, "Reconnect");
    } else {
        lv_label_set_text(cbtxt, "Connect");
    }
    lv_obj_center(cbtxt);

    lv_obj_add_event_cb(btn_connect, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        // Show spinner, hide button text
        lv_obj_clear_flag(self->m_wifi_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *btn = lv_event_get_target_obj(e);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);

        // WiFi disabled — C5 removed
        lv_label_set_text(self->m_wifi_status_label, "WiFi: Unavailable (C5 removed)");
        lv_obj_add_flag(self->m_wifi_spinner, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, this);

    // Note text
    lv_obj_t *note = lv_label_create(m_wifi_page);
    lv_label_set_text(note, "Status updates will appear here.\n"
                            "Connect to AP: ESP32-P4-Config\n"
                            "Pass: 12345678 → http://192.168.4.1");
    lv_obj_set_style_text_color(note, lv_color_hex(0x666688), LV_PART_MAIN);
    lv_obj_set_style_text_font(note, &montserrat_bold_20, LV_PART_MAIN);
    lv_obj_align(note, LV_ALIGN_TOP_LEFT, 20, 280);

    // Poll timer for cross-task status updates
    m_wifi_status_timer = lv_timer_create([](lv_timer_t *t) {
        auto *self = (SettingsApp *)t->user_data;
        if (s_wifi_status_dirty) {
            s_wifi_status_dirty = false;
            lv_label_set_text(self->m_wifi_status_label, s_wifi_status_buf);

            if (s_wifi_status_done) {
                // WiFi connected! Stop spinner, show results
                lv_obj_add_flag(self->m_wifi_spinner, LV_OBJ_FLAG_HIDDEN);
                // Update SSID and IP labels
                char buf[128];
                if (g_wifi_ssid[0]) {
                    snprintf(buf, sizeof(buf), "SSID: %s", g_wifi_ssid);
                    lv_label_set_text(self->m_wifi_ssid_label, buf);
                    snprintf(buf, sizeof(buf), "IP: %s", g_wifi_ip[0] ? (g_wifi_ip + 6) : "N/A");
                    lv_label_set_text(self->m_wifi_ip_label, buf);
                }
                // Stop this timer
                lv_timer_del(self->m_wifi_status_timer);
                self->m_wifi_status_timer = nullptr;
            }
        }
    }, 200, this);

    // Back button
    lv_obj_t *btn_back = lv_button_create(m_wifi_page);
    lv_obj_set_size(btn_back, 80, 36);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_t *bbtxt = lv_label_create(btn_back);
    lv_label_set_text(bbtxt, "Back");
    lv_obj_center(bbtxt);
    lv_obj_add_event_cb(btn_back, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        if (self->m_wifi_status_timer) {
            lv_timer_del(self->m_wifi_status_timer);
            self->m_wifi_status_timer = nullptr;
        }
        self->back();
    }, LV_EVENT_CLICKED, this);
}

// ============================================================
// About Page
// ============================================================
void SettingsApp::create_about_page()
{
    if (m_about_page) { show_page(m_about_page); return; }
    m_about_page = create_page_container();
    show_page(m_about_page);

    lv_obj_t *title = lv_label_create(m_about_page);
    lv_label_set_text(title, "About Device");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &montserrat_bold_26, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Info lines
    char buf[128], mac_str[32];
    int y = 80;

    auto add_info = [&](const char *key, const char *val) {
        lv_obj_t *lbl = lv_label_create(m_about_page);
        snprintf(buf, sizeof(buf), "%s: %s", key, val);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xAAAAFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &montserrat_bold_20, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 20, y);
        y += 35;
    };

    add_info("Chip", "ESP32-P4");
    add_info("Board", "WT99P4C5-S1");

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    add_info("MAC", mac_str);

    add_info("IDF", esp_get_idf_version());
    add_info("Brookesia", "0.5.0");

    // Back button
    lv_obj_t *btn_back = lv_button_create(m_about_page);
    lv_obj_set_size(btn_back, 80, 36);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_t *bbtxt = lv_label_create(btn_back);
    lv_label_set_text(bbtxt, "Back");
    lv_obj_center(bbtxt);
    lv_obj_add_event_cb(btn_back, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        self->back();
    }, LV_EVENT_CLICKED, this);
}

// ============================================================
// Wallpaper: scan SD card for .rgb565 files
// ============================================================
#define WP_WIDTH  1024
#define WP_HEIGHT 600
#define WP_SIZE   (WP_WIDTH * WP_HEIGHT * 2)  // RGB565 = 1,228,800 bytes

static const char *WP_DIR       = "/sdcard";
static const char *WP_NVS_NS    = "wallpaper";
static const char *WP_NVS_KEY   = "path";

void SettingsApp::scan_wallpapers()
{
    m_wallpaper_files.clear();

    DIR *dir = opendir(WP_DIR);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            const char *name = entry->d_name;
            const char *ext = strrchr(name, '.');
            if (ext && strcmp(ext, ".rgb565") == 0) {
                char full_path[300];
                snprintf(full_path, sizeof(full_path), "%s/%s", WP_DIR, name);
                struct stat st;
                if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == WP_SIZE) {
                    m_wallpaper_files.push_back(std::string(full_path));
                } else if (stat(full_path, &st) == 0) {
                    ESP_LOGW(TAG, "Skipping %s: size=%ld (expected %d)", name, (long)st.st_size, WP_SIZE);
                }
            }
        }
        closedir(dir);
    } else {
        // Fallback: opendir failed, try common filenames directly
        ESP_LOGW(TAG, "opendir(%s) failed, trying fallback scan", WP_DIR);
        for (int i = 1; i <= 20; i++) {
            char path[300];
            snprintf(path, sizeof(path), "%s/%d.rgb565", WP_DIR, i);
            struct stat st;
            if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == WP_SIZE) {
                m_wallpaper_files.push_back(std::string(path));
            }
        }
    }

    ESP_LOGI(TAG, "Found %d valid wallpapers", (int)m_wallpaper_files.size());
}

// ============================================================
// Apply a wallpaper to the main screen
// ============================================================
void SettingsApp::apply_wallpaper(const std::string &path)
{
    if (path == "default") {
        // Revert to brookesia built-in default wallpaper
        if (getCore()) {
            lv_obj_t *main_obj = getCore()->getCoreDisplay().getMainScreenObject();
            lv_obj_set_style_bg_img_src(main_obj, nullptr,
                                        ((int)LV_PART_MAIN | (int)LV_STATE_DEFAULT));
        }
        // Free old dynamic wallpaper if any
        if (m_active_wp_data) { free(m_active_wp_data); m_active_wp_data = nullptr; }
        if (g_active_wp_dsc)  { free(g_active_wp_dsc);  g_active_wp_dsc  = nullptr; }
        if (g_active_wp_data) { free(g_active_wp_data); g_active_wp_data = nullptr; }
        return;
    }

    // Load .bin file (raw RGB565)
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", path.c_str());
        return;
    }

    void *pixels = malloc(WP_SIZE);
    if (!pixels) {
        ESP_LOGE(TAG, "OOM loading wallpaper (%d bytes)", WP_SIZE);
        fclose(f);
        return;
    }

    size_t read = fread(pixels, 1, WP_SIZE, f);
    fclose(f);

    if (read != WP_SIZE) {
        ESP_LOGE(TAG, "Short read: %zu / %d", read, WP_SIZE);
        free(pixels);
        return;
    }

    // Create LVGL image descriptor (calloc = zero-init, magic byte critical)
    lv_image_dsc_t *dsc = (lv_image_dsc_t *)calloc(1, sizeof(lv_image_dsc_t));
    if (!dsc) {
        free(pixels);
        return;
    }
    dsc->header.cf    = LV_COLOR_FORMAT_RGB565;
    dsc->header.w     = WP_WIDTH;
    dsc->header.h     = WP_HEIGHT;
    dsc->header.stride = WP_WIDTH * 2;
    dsc->header.magic  = LV_IMAGE_HEADER_MAGIC;
    dsc->data         = (const uint8_t *)pixels;
    dsc->data_size    = WP_SIZE;

    // Apply to main screen container
    if (getCore()) {
        lv_obj_t *main_obj = getCore()->getCoreDisplay().getMainScreenObject();
        lv_obj_set_style_bg_img_src(main_obj, dsc,
                                    ((int)LV_PART_MAIN | (int)LV_STATE_DEFAULT));
    }

    // Free old dynamic wallpaper
    if (m_active_wp_data) {
        free(m_active_wp_data);
    }
    m_active_wp_data = pixels;

    // Also free boot-time wallpaper (allocated in app_main.cpp)
    if (g_active_wp_dsc) {
        free(g_active_wp_dsc);
        g_active_wp_dsc = nullptr;
    }
    if (g_active_wp_data) {
        free(g_active_wp_data);
        g_active_wp_data = nullptr;
    }

    // Persist to NVS
    nvs_handle_t h;
    if (nvs_open(WP_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, WP_NVS_KEY, path.c_str());
        nvs_commit(h);
        nvs_close(h);
    }
}

// ============================================================
// Wallpaper Page
// ============================================================
void SettingsApp::create_wallpaper_page()
{
    if (m_wallpaper_page) { show_page(m_wallpaper_page); return; }
    m_wallpaper_page = create_page_container();
    show_page(m_wallpaper_page);

    lv_obj_t *title = lv_label_create(m_wallpaper_page);
    lv_label_set_text(title, "Wallpaper");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &montserrat_bold_26, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Scan for wallpapers
    scan_wallpapers();

    // Status label
    m_wallpaper_status = lv_label_create(m_wallpaper_page);
    lv_obj_set_style_text_color(m_wallpaper_status, lv_color_hex(0xAAAAFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_wallpaper_status, &montserrat_bold_20, LV_PART_MAIN);
    lv_obj_align(m_wallpaper_status, LV_ALIGN_TOP_LEFT, 20, 60);

    // Disk usage label (sum sizes of all files in WP_DIR)
    {
        lv_obj_t *lbl = lv_label_create(m_wallpaper_page);
        unsigned long used_kb = 0;
        DIR *d = opendir(WP_DIR);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                char p[300];
                snprintf(p, sizeof(p), "%s/%s", WP_DIR, ent->d_name);
                struct stat st;
                if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) used_kb += st.st_size / 1024;
            }
            closedir(d);
        }
        char space_buf[64];
        snprintf(space_buf, sizeof(space_buf), "Used: %lu KB", used_kb);
        lv_label_set_text(lbl, space_buf);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x668866), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &montserrat_bold_20, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 20, 85);
        ESP_LOGI(TAG, "Storage %s: %lu KB used", WP_DIR, used_kb);
    }

    // Read current wallpaper from NVS

    // Read current wallpaper from NVS
    char current_path[256] = "default";
    nvs_handle_t h;
    if (nvs_open(WP_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(current_path);
        nvs_get_str(h, WP_NVS_KEY, current_path, &len);
        nvs_close(h);
    }

    // Extract filename from path for display
    const char *display_name = "Default";
    if (strcmp(current_path, "default") != 0) {
        const char *slash = strrchr(current_path, '/');
        display_name = slash ? slash + 1 : current_path;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "Current: %s", display_name);
    lv_label_set_text(m_wallpaper_status, buf);

    // Scrollable file list
    m_wallpaper_list = lv_obj_create(m_wallpaper_page);
    lv_obj_set_size(m_wallpaper_list, lv_pct(90), lv_pct(55));
    lv_obj_align(m_wallpaper_list, LV_ALIGN_TOP_MID, 0, 95);
    lv_obj_set_style_bg_opa(m_wallpaper_list, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(m_wallpaper_list, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(m_wallpaper_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_wallpaper_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(m_wallpaper_list, 4, LV_PART_MAIN);
    lv_obj_add_flag(m_wallpaper_list, LV_OBJ_FLAG_SCROLLABLE);

    if (m_wallpaper_files.empty()) {
        lv_obj_t *lbl = lv_label_create(m_wallpaper_list);
        lv_label_set_text(lbl, "No .rgb565 wallpapers found in /sdcard/\n\n"
                               "Copy 1024x600 RGB565 .rgb565 files\n"
                               "to the SD card root.");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &montserrat_bold_20, LV_PART_MAIN);
    }

    for (auto &wp : m_wallpaper_files) {
        lv_obj_t *row = lv_obj_create(m_wallpaper_list);
        lv_obj_set_size(row, lv_pct(95), 45);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2a2a3e), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x444466), 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 10, 0);
        lv_obj_set_style_pad_right(row, 10, 0);

        // Show filename only (not full path)
        const char *slash = strrchr(wp.c_str(), '/');
        const char *fname = slash ? slash + 1 : wp.c_str();
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, fname);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl, &montserrat_bold_20, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *btn = lv_button_create(row);
        lv_obj_set_size(btn, 70, 32);
        lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0f3460), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x6688cc), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_t *btxt = lv_label_create(btn);
        lv_label_set_text(btxt, "Set");
        lv_obj_center(btxt);

        // Store SettingsApp* in row, path string in btn
        std::string *path_copy = new std::string(wp);
        lv_obj_set_user_data(row, this);
        lv_obj_set_user_data(btn, path_copy);

        // Free path_copy when button is deleted (e.g., page exit)
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            auto *ptr = (std::string *)lv_obj_get_user_data(lv_event_get_target_obj(e));
            delete ptr;
        }, LV_EVENT_DELETE, nullptr);

        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            lv_obj_t *btn = lv_event_get_target_obj(e);
            lv_obj_t *row = lv_obj_get_parent(btn);
            auto *self = (SettingsApp *)lv_obj_get_user_data(row);
            auto *path_ptr = (std::string *)lv_obj_get_user_data(btn);
            if (self && path_ptr) {
                self->apply_wallpaper(*path_ptr);
                const char *slash = strrchr(path_ptr->c_str(), '/');
                const char *fname = slash ? slash + 1 : path_ptr->c_str();
                char buf[256];
                snprintf(buf, sizeof(buf), "Current: %s (applied!)", fname);
                lv_label_set_text(self->m_wallpaper_status, buf);
            }
        }, LV_EVENT_CLICKED, nullptr);
    }

    // "Use Default" button
    lv_obj_t *btn_def = lv_button_create(m_wallpaper_page);
    lv_obj_set_size(btn_def, 200, 42);
    lv_obj_align(btn_def, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_bg_color(btn_def, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(btn_def, 8, 0);
    lv_obj_t *dtxt = lv_label_create(btn_def);
    lv_label_set_text(dtxt, "Use Default");
    lv_obj_center(dtxt);
    lv_obj_add_event_cb(btn_def, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        self->apply_wallpaper("default");
        lv_label_set_text(self->m_wallpaper_status, "Current: Default (applied!)");
    }, LV_EVENT_CLICKED, this);

    // Back button
    lv_obj_t *btn_back = lv_button_create(m_wallpaper_page);
    lv_obj_set_size(btn_back, 80, 36);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_t *bbtxt = lv_label_create(btn_back);
    lv_label_set_text(bbtxt, "Back");
    lv_obj_center(bbtxt);
    lv_obj_add_event_cb(btn_back, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        self->back();
    }, LV_EVENT_CLICKED, this);
}

// ============================================================
// Radar Training Page
// ============================================================
char g_radar_training_status[256] = "Ready";
float g_radar_someone_threshold = -1.0f;
float g_radar_move_threshold = -1.0f;
int g_radar_training_elapsed = 0;
bool g_radar_training_active = false;

/* ── Frame callback: handles radar training status from S3 ── */
static void radar_frame_callback(uint8_t type, const uint8_t *data, size_t len)
{
    if (type != UART_FRAME_CTRL || len < 4) return;
    ctrl_frame_header_t *ctrl = (ctrl_frame_header_t *)data;

    if (ctrl->cmd == CTRL_RADAR_STATUS && ctrl->payload_len > 0) {
        const char *json = (const char *)&data[4];
        /* Parse {"training":true/false,"elapsed_s":N,"status":"..."} */
        const char *p = strstr(json, "\"elapsed_s\":");
        if (p) g_radar_training_elapsed = atoi(p + 12);
        p = strstr(json, "\"status\":\"");
        if (p) {
            p += 10;
            const char *e = strchr(p, '"');
            if (e && (e - p) < (int)sizeof(g_radar_training_status) - 1) {
                memcpy(g_radar_training_status, p, e - p);
                g_radar_training_status[e - p] = 0;
            }
        }
        p = strstr(json, "\"training\":true");
        if (p) g_radar_training_active = true;
        p = strstr(json, "\"training\":false");
        if (p) g_radar_training_active = false;
    }
    else if (ctrl->cmd == CTRL_RADAR_TRAIN_DONE && ctrl->payload_len > 0) {
        const char *json = (const char *)&data[4];
        /* Parse {"someone_threshold":X,"move_threshold":Y} */
        g_radar_training_active = false;
        const char *p = strstr(json, "\"someone_threshold\":");
        if (p) g_radar_someone_threshold = (float)atof(p + 20);
        p = strstr(json, "\"move_threshold\":");
        if (p) g_radar_move_threshold = (float)atof(p + 17);
        snprintf(g_radar_training_status, sizeof(g_radar_training_status), "Done!");
    }
}

void SettingsApp::create_radar_page()
{
    if (m_radar_page) { show_page(m_radar_page); return; }
    static bool frame_cb_registered = false;
    if (!frame_cb_registered) {
        uart_bridge_on_frame(radar_frame_callback);
        frame_cb_registered = true;
    }
    m_radar_page = create_page_container();
    show_page(m_radar_page);

    lv_obj_t *title = lv_label_create(m_radar_page);
    lv_label_set_text(title, "Radar Training");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &montserrat_bold_26, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    m_radar_status_label = lv_label_create(m_radar_page);
    lv_label_set_text(m_radar_status_label, "Status: Ready");
    lv_obj_set_style_text_color(m_radar_status_label, lv_color_hex(0xAAAAFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_radar_status_label, &montserrat_bold_20, LV_PART_MAIN);
    lv_obj_align(m_radar_status_label, LV_ALIGN_TOP_LEFT, 20, 80);

    m_radar_elapsed_label = lv_label_create(m_radar_page);
    lv_label_set_text(m_radar_elapsed_label, "");
    lv_obj_set_style_text_color(m_radar_elapsed_label, lv_color_hex(0x88CC88), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_radar_elapsed_label, &montserrat_bold_20, LV_PART_MAIN);
    lv_obj_align(m_radar_elapsed_label, LV_ALIGN_TOP_LEFT, 20, 110);

    m_radar_threshold_label = lv_label_create(m_radar_page);
    lv_label_set_text(m_radar_threshold_label, "Thresholds: N/A");
    lv_obj_set_style_text_color(m_radar_threshold_label, lv_color_hex(0xCCAA88), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_radar_threshold_label, &montserrat_bold_20, LV_PART_MAIN);
    lv_obj_align(m_radar_threshold_label, LV_ALIGN_TOP_LEFT, 20, 140);

    m_radar_spinner = lv_label_create(m_radar_page);
    lv_label_set_text(m_radar_spinner, "Training...");
    lv_obj_set_style_text_color(m_radar_spinner, lv_color_hex(0xFFCC00), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_radar_spinner, &montserrat_bold_20, LV_PART_MAIN);
    lv_obj_align(m_radar_spinner, LV_ALIGN_TOP_MID, 0, 190);
    lv_obj_add_flag(m_radar_spinner, LV_OBJ_FLAG_HIDDEN);

    /* ── Start Training ── */
    m_radar_btn_start = lv_button_create(m_radar_page);
    lv_obj_set_size(m_radar_btn_start, 220, 50);
    lv_obj_align(m_radar_btn_start, LV_ALIGN_TOP_MID, 0, 240);
    lv_obj_set_style_bg_color(m_radar_btn_start, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_radius(m_radar_btn_start, 8, 0);
    lv_obj_t *stxt = lv_label_create(m_radar_btn_start);
    lv_label_set_text(stxt, "Start Training");
    lv_obj_center(stxt);
    lv_obj_add_event_cb(m_radar_btn_start, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        uint8_t f[4] = {UART_FRAME_CTRL, CTRL_RADAR_TRAIN_START, 0, 0};
        uart_bridge_send_frame(UART_FRAME_CTRL, f, 4);
        g_radar_training_active = true;
        g_radar_training_elapsed = 0;
        snprintf(g_radar_training_status, sizeof(g_radar_training_status), "Training...");
        self->m_radar_training_active = true;
        lv_obj_clear_flag(self->m_radar_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(self->m_radar_status_label, "Status: Training...");
        lv_label_set_text(self->m_radar_elapsed_label, "Elapsed: 0s");
    }, LV_EVENT_CLICKED, this);

    /* ── Stop Training ── */
    m_radar_btn_stop = lv_button_create(m_radar_page);
    lv_obj_set_size(m_radar_btn_stop, 220, 50);
    lv_obj_align(m_radar_btn_stop, LV_ALIGN_TOP_MID, 0, 310);
    lv_obj_set_style_bg_color(m_radar_btn_stop, lv_color_hex(0x664400), 0);
    lv_obj_set_style_radius(m_radar_btn_stop, 8, 0);
    lv_obj_t *stptxt = lv_label_create(m_radar_btn_stop);
    lv_label_set_text(stptxt, "Stop Training");
    lv_obj_center(stptxt);
    lv_obj_add_event_cb(m_radar_btn_stop, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        uint8_t f[4] = {UART_FRAME_CTRL, CTRL_RADAR_TRAIN_STOP, 0, 0};
        uart_bridge_send_frame(UART_FRAME_CTRL, f, 4);
        g_radar_training_active = false;
        self->m_radar_training_active = false;
        lv_obj_add_flag(self->m_radar_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(self->m_radar_status_label, "Status: Waiting for results...");
    }, LV_EVENT_CLICKED, this);

    /* ── Clear Calibration ── */
    lv_obj_t *btn_clear = lv_button_create(m_radar_page);
    lv_obj_set_size(btn_clear, 220, 50);
    lv_obj_align(btn_clear, LV_ALIGN_TOP_MID, 0, 380);
    lv_obj_set_style_bg_color(btn_clear, lv_color_hex(0x662222), 0);
    lv_obj_set_style_radius(btn_clear, 8, 0);
    lv_obj_t *ctxt = lv_label_create(btn_clear);
    lv_label_set_text(ctxt, "Clear Calibration");
    lv_obj_center(ctxt);
    lv_obj_add_event_cb(btn_clear, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        uint8_t f[4] = {UART_FRAME_CTRL, CTRL_RADAR_TRAIN_CLEAR, 0, 0};
        uart_bridge_send_frame(UART_FRAME_CTRL, f, 4);
        g_radar_training_active = false;
        self->m_radar_training_active = false;
        lv_obj_add_flag(self->m_radar_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(self->m_radar_status_label, "Status: Cleared");
        lv_label_set_text(self->m_radar_threshold_label, "Thresholds: N/A");
    }, LV_EVENT_CLICKED, this);

    /* ── Poll timer ── */
    m_radar_timer = lv_timer_create([](lv_timer_t *t) {
        auto *self = (SettingsApp *)t->user_data;
        static char lstatus[256] = {};
        static float ls = -1, lm = -1;
        static int le = -1;
        static bool la = false;

        if (g_radar_training_active != la) {
            la = g_radar_training_active;
            if (!la) lv_obj_add_flag(self->m_radar_spinner, LV_OBJ_FLAG_HIDDEN);
        }
        if (g_radar_training_elapsed != le) {
            le = g_radar_training_elapsed;
            char b[48]; snprintf(b, sizeof(b), "Elapsed: %ds", le);
            lv_label_set_text(self->m_radar_elapsed_label, b);
        }
        if (strcmp(g_radar_training_status, lstatus) != 0) {
            snprintf(lstatus, sizeof(lstatus), "%s", g_radar_training_status);
            char b[280]; snprintf(b, sizeof(b), "Status: %s", lstatus);
            lv_label_set_text(self->m_radar_status_label, b);
        }
        if (g_radar_someone_threshold != ls || g_radar_move_threshold != lm) {
            ls = g_radar_someone_threshold; lm = g_radar_move_threshold;
            char b[64]; snprintf(b, sizeof(b), "someone=%.4f  move=%.4f", ls, lm);
            lv_label_set_text(self->m_radar_threshold_label, b);
        }
    }, 500, this);

    lv_obj_t *rbtn_back = lv_button_create(m_radar_page);
    lv_obj_set_size(rbtn_back, 80, 36);
    lv_obj_align(rbtn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_t *rbbtxt = lv_label_create(rbtn_back);
    lv_label_set_text(rbbtxt, "Back");
    lv_obj_center(rbbtxt);
    lv_obj_add_event_cb(rbtn_back, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        self->back();
    }, LV_EVENT_CLICKED, this);
}
