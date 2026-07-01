#include "face_recognition_app.hpp"
#include "who_recognition_app_lcd.hpp"
#include "esp_brookesia.hpp"
#include "esp_log.h"

static const char *TAG = "FaceApp";
bool FaceRecognitionApp::s_app_installed = false;

// Access global recognition app (for pause/resume)
extern who::app::WhoRecognitionAppLCD *g_recognition_app;
// Voice pause flag (checked by voice task)
extern bool g_voice_paused;

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

bool FaceRecognitionApp::run()
{
    ESP_LOGI(TAG, "Camera App started");
    if (g_camera_scr) {
        lv_screen_load(g_camera_scr);
    }
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
    ESP_LOGI(TAG, "Camera App closed — stopping all");
    g_voice_paused = true;
    if (g_recognition_app) g_recognition_app->stop();
    return true;
}
