#pragma once
#include "esp_brookesia.hpp"
#include "lvgl.h"

class SettingsApp : public ESP_Brookesia_PhoneApp {
public:
    SettingsApp();
    virtual ~SettingsApp();

    bool run() override;
    bool back() override;
    bool close() override;

private:
    void create_main_page();
    void create_face_page();
    void create_wifi_page();
    void create_about_page();

    void refresh_face_list();

    lv_obj_t *m_main_scr = nullptr;
    lv_obj_t *m_face_scr = nullptr;
    lv_obj_t *m_wifi_scr = nullptr;
    lv_obj_t *m_about_scr = nullptr;
    lv_obj_t *m_face_list = nullptr;
    lv_obj_t *m_face_count_label = nullptr;
};
