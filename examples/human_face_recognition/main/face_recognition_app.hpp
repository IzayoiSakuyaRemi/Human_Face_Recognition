#pragma once
#include "esp_brookesia.hpp"

class FaceRecognitionApp : public ESP_Brookesia_PhoneApp {
public:
    FaceRecognitionApp();

    bool run() override;
    bool back() override;
    bool pause() override;
    bool resume() override;
    bool close() override;

private:
    void create_exit_button(lv_obj_t *parent);
    lv_obj_t *m_exit_btn = nullptr;
    bool m_ui_created = false;
};
