#pragma once
#include "esp_brookesia.hpp"

// Global camera screen handle, set by app_main, used by FaceRecognitionApp
extern lv_obj_t *g_camera_scr;

class FaceRecognitionApp : public ESP_Brookesia_PhoneApp {
public:
    FaceRecognitionApp();

    bool run() override;
    bool back() override;
    bool pause() override;
    bool resume() override;
    bool close() override;

private:
    static bool s_app_installed;
};
