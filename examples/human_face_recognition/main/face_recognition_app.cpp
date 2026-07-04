#include "face_recognition_app.hpp"
#include "who_recognition_app_lcd.hpp"
#include "esp_brookesia.hpp"
#include "esp_log.h"

static const char *TAG = "FaceApp";

// Access global recognition app (for pause/resume)
extern who::app::WhoRecognitionAppLCD *g_recognition_app;
// Voice pause flag (checked by voice task)
extern bool g_voice_paused;
extern bool g_voice_enrolling;
extern bool g_voice_verifying;
extern int g_pending_cmd_id;

FaceRecognitionApp::FaceRecognitionApp()
    : ESP_Brookesia_PhoneApp(
          {/* core_data */
           .name = "Camera",
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

void FaceRecognitionApp::create_exit_button(lv_obj_t *parent)
{
    m_exit_btn = lv_button_create(parent);
    lv_obj_set_style_bg_color(m_exit_btn, lv_color_hex(0x662222), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m_exit_btn, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(m_exit_btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(m_exit_btn, lv_color_hex(0xcc4444), LV_PART_MAIN);
    lv_obj_set_style_radius(m_exit_btn, 6, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(m_exit_btn);
    lv_label_set_text(label, "Exit");
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_center(label);
    lv_obj_align(m_exit_btn, LV_ALIGN_TOP_RIGHT, -10, 300);

    lv_obj_add_event_cb(m_exit_btn, [](lv_event_t *e) {
        ESP_LOGI(TAG, "Exit button pressed");
        // notifyCoreClosed will trigger brookesia to call back() -> close() -> return to home
        auto *app = (FaceRecognitionApp *)lv_event_get_user_data(e);
        app->notifyCoreClosed();
    }, LV_EVENT_CLICKED, this);
}

bool FaceRecognitionApp::run()
{
    ESP_LOGI(TAG, "Camera App started");

    // brookesia created a default screen for this app (enable_default_screen=1)
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // Create recognition UI (canvas, labels, buttons) on this screen
    g_voice_paused = false;  // activate voice recognition when Camera App is open
    if (g_recognition_app) {
        g_recognition_app->create_ui(scr);
        g_recognition_app->run();  // restart pipeline tasks (stopped by close())
        m_ui_created = true;
    }

    // Create Exit button — now owned by FaceRecognitionApp
    create_exit_button(scr);

    return true;
}

bool FaceRecognitionApp::back()
{
    ESP_LOGI(TAG, "Camera App back");
    notifyCoreClosed();
    return true;
}

bool FaceRecognitionApp::pause()
{
    ESP_LOGI(TAG, "Camera App paused — stopping camera + detection + voice");
    if (g_recognition_app) g_recognition_app->pause();
    g_voice_paused = true;
    return true;
}

bool FaceRecognitionApp::resume()
{
    ESP_LOGI(TAG, "Camera App resumed — restarting camera + detection + voice");
    g_voice_paused = false;
    if (g_recognition_app) g_recognition_app->resume();
    return true;
}

bool FaceRecognitionApp::close()
{
    ESP_LOGI(TAG, "Camera App closed — stopping pipeline");
    g_voice_paused = true;
    g_voice_enrolling = false;
    g_voice_verifying = false;
    g_pending_cmd_id = 0;
    if (g_recognition_app) {
        g_recognition_app->stop();       // free camera DMA buffers + CPU
        g_recognition_app->reset_ui();   // null canvas/label pointers before brookesia deletes the screen
    }
    m_exit_btn = nullptr;
    m_ui_created = false;
    return true;
}
