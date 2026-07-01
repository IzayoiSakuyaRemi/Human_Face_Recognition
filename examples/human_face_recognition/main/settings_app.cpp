#include "settings_app.hpp"
#include "who_recognition_app_lcd.hpp"
#include "who_recognition.hpp"
#include "human_face_recognition.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include <cstdio>
#include <string>

static const char *TAG = "SettingsApp";
extern who::app::WhoRecognitionAppLCD *g_recognition_app;
extern char g_wifi_ip[32];
LV_FONT_DECLARE(montserrat_bold_26);
LV_FONT_DECLARE(montserrat_bold_20);

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
// Main Page
// ============================================================
void SettingsApp::create_main_page()
{
    m_main_scr = lv_screen_active();
    lv_obj_set_style_bg_color(m_main_scr, lv_color_hex(0x1a1a2e), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(m_main_scr);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &montserrat_bold_26, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    auto *btn1 = menu_row_create(m_main_scr, "Face Database", 80);
    lv_obj_add_event_cb(btn1, [](lv_event_t *e) {
        ((SettingsApp *)lv_event_get_user_data(e))->create_face_page();
    }, LV_EVENT_CLICKED, this);

    auto *btn2 = menu_row_create(m_main_scr, "WiFi", 155);
    lv_obj_add_event_cb(btn2, [](lv_event_t *e) {
        ((SettingsApp *)lv_event_get_user_data(e))->create_wifi_page();
    }, LV_EVENT_CLICKED, this);

    auto *btn3 = menu_row_create(m_main_scr, "About Device", 230);
    lv_obj_add_event_cb(btn3, [](lv_event_t *e) {
        ((SettingsApp *)lv_event_get_user_data(e))->create_about_page();
    }, LV_EVENT_CLICKED, this);
}

// ============================================================
// Back navigation
// ============================================================
bool SettingsApp::back()
{
    lv_obj_t *cur = lv_screen_active();
    if (cur == m_face_scr || cur == m_wifi_scr || cur == m_about_scr) {
        lv_screen_load(m_main_scr);
        return true;
    }
    notifyCoreClosed();
    return true;
}

bool SettingsApp::close()
{
    ESP_LOGI(TAG, "Settings closed");
    return true;
}

// ============================================================
// Face Database Page
// ============================================================
void SettingsApp::refresh_face_list()
{
    if (!m_face_count_label) return;

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

    char buf[64];
    snprintf(buf, sizeof(buf), "Enrolled faces: %d", fb->get_num_feats());
    lv_label_set_text(m_face_count_label, buf);
}

void SettingsApp::create_face_page()
{
    m_face_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(m_face_scr, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
    lv_screen_load(m_face_scr);

    lv_obj_t *title = lv_label_create(m_face_scr);
    lv_label_set_text(title, "Face Database");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &montserrat_bold_26, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    m_face_count_label = lv_label_create(m_face_scr);
    lv_obj_set_style_text_color(m_face_count_label, lv_color_hex(0xAAAAFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(m_face_count_label, &montserrat_bold_20, LV_PART_MAIN);
    lv_obj_align(m_face_count_label, LV_ALIGN_TOP_LEFT, 20, 80);

    // Delete Last button
    lv_obj_t *btn_last = lv_button_create(m_face_scr);
    lv_obj_set_size(btn_last, 160, 40);
    lv_obj_align(btn_last, LV_ALIGN_TOP_MID, -90, 140);
    lv_obj_set_style_bg_color(btn_last, lv_color_hex(0x444466), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_last, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_last, lv_color_hex(0x8888AA), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_last, 6, LV_PART_MAIN);
    lv_obj_t *ltxt = lv_label_create(btn_last);
    lv_label_set_text(ltxt, "Delete Last");
    lv_obj_center(ltxt);
    lv_obj_add_event_cb(btn_last, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        if (g_recognition_app) {
            auto *recog = g_recognition_app->get_recognition()->get_recognition_task();
            auto *fb = recog->get_recognizer();
            if (fb) fb->delete_last_feat();
        }
        self->refresh_face_list();
    }, LV_EVENT_CLICKED, this);

    // Delete All button
    lv_obj_t *btn_all = lv_button_create(m_face_scr);
    lv_obj_set_size(btn_all, 160, 40);
    lv_obj_align(btn_all, LV_ALIGN_TOP_MID, 90, 140);
    lv_obj_set_style_bg_color(btn_all, lv_color_hex(0x662222), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_all, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_all, lv_color_hex(0xcc4444), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_all, 6, LV_PART_MAIN);
    lv_obj_t *atxt = lv_label_create(btn_all);
    lv_label_set_text(atxt, "Clear All");
    lv_obj_center(atxt);
    lv_obj_add_event_cb(btn_all, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        if (g_recognition_app) {
            auto *recog = g_recognition_app->get_recognition()->get_recognition_task();
            auto *fb = recog->get_recognizer();
            if (fb) fb->clear_all_feats();
        }
        self->refresh_face_list();
    }, LV_EVENT_CLICKED, this);

    // Back button
    lv_obj_t *btn_back = lv_button_create(m_face_scr);
    lv_obj_set_size(btn_back, 80, 36);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_t *btxt = lv_label_create(btn_back);
    lv_label_set_text(btxt, "Back");
    lv_obj_center(btxt);
    lv_obj_add_event_cb(btn_back, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        lv_screen_load(self->m_main_scr);
    }, LV_EVENT_CLICKED, this);

    refresh_face_list();
}

// ============================================================
// WiFi Page
// ============================================================
void SettingsApp::create_wifi_page()
{
    m_wifi_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(m_wifi_scr, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
    lv_screen_load(m_wifi_scr);

    lv_obj_t *title = lv_label_create(m_wifi_scr);
    lv_label_set_text(title, "WiFi");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &montserrat_bold_26, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Status label
    lv_obj_t *status = lv_label_create(m_wifi_scr);
    lv_obj_set_style_text_color(status, lv_color_hex(0xAAAAFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(status, &montserrat_bold_20, LV_PART_MAIN);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 20, 80);

    char buf[128];
    if (g_wifi_ip[0]) {
        snprintf(buf, sizeof(buf), "WiFi: %s", g_wifi_ip);
    } else {
        snprintf(buf, sizeof(buf), "WiFi: Not connected");
    }
    lv_label_set_text(status, buf);

    // IP label
    lv_obj_t *ip_label = lv_label_create(m_wifi_scr);
    snprintf(buf, sizeof(buf), "IP: %s", g_wifi_ip[0] ? g_wifi_ip + 6 : "N/A");  // skip "WiFi: "
    lv_label_set_text(ip_label, buf);
    lv_obj_set_style_text_color(ip_label, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(ip_label, &montserrat_bold_20, LV_PART_MAIN);
    lv_obj_align(ip_label, LV_ALIGN_TOP_LEFT, 20, 120);

    // Note about provisioning
    lv_obj_t *note = lv_label_create(m_wifi_scr);
    lv_label_set_text(note, "WiFi provisioning: connect to AP\n"
                            "ESP32-P4-Config / Pass: 12345678\n"
                            "then open http://192.168.4.1");
    lv_obj_set_style_text_color(note, lv_color_hex(0x666688), LV_PART_MAIN);
    lv_obj_set_style_text_font(note, &montserrat_bold_20, LV_PART_MAIN);
    lv_obj_align(note, LV_ALIGN_TOP_LEFT, 20, 180);

    // Back button
    lv_obj_t *btn_back = lv_button_create(m_wifi_scr);
    lv_obj_set_size(btn_back, 80, 36);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_t *bbtxt = lv_label_create(btn_back);
    lv_label_set_text(bbtxt, "Back");
    lv_obj_center(bbtxt);
    lv_obj_add_event_cb(btn_back, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        lv_screen_load(self->m_main_scr);
    }, LV_EVENT_CLICKED, this);
}

// ============================================================
// About Page
// ============================================================
void SettingsApp::create_about_page()
{
    m_about_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(m_about_scr, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
    lv_screen_load(m_about_scr);

    lv_obj_t *title = lv_label_create(m_about_scr);
    lv_label_set_text(title, "About Device");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &montserrat_bold_26, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Info lines
    char buf[128], mac_str[32];
    int y = 80;

    auto add_info = [&](const char *key, const char *val) {
        lv_obj_t *lbl = lv_label_create(m_about_scr);
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
    lv_obj_t *btn_back = lv_button_create(m_about_scr);
    lv_obj_set_size(btn_back, 80, 36);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_t *bbtxt = lv_label_create(btn_back);
    lv_label_set_text(bbtxt, "Back");
    lv_obj_center(bbtxt);
    lv_obj_add_event_cb(btn_back, [](lv_event_t *e) {
        auto *self = (SettingsApp *)lv_event_get_user_data(e);
        lv_screen_load(self->m_main_scr);
    }, LV_EVENT_CLICKED, this);
}
