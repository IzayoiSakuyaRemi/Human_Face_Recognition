#pragma once
#include "esp_brookesia.hpp"
#include "lvgl.h"
#include <vector>
#include <string>

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
    void create_wallpaper_page();

    void refresh_face_list();
    void apply_wallpaper(const std::string &path);
    void scan_wallpapers();

    // Screens
    lv_obj_t *m_main_scr = nullptr;
    lv_obj_t *m_face_scr = nullptr;
    lv_obj_t *m_wifi_scr = nullptr;
    lv_obj_t *m_about_scr = nullptr;
    lv_obj_t *m_wallpaper_scr = nullptr;

    // Face DB
    lv_obj_t *m_face_list = nullptr;
    lv_obj_t *m_face_count_label = nullptr;

    // WiFi page
    lv_obj_t *m_wifi_status_label = nullptr;
    lv_obj_t *m_wifi_ssid_label = nullptr;
    lv_obj_t *m_wifi_ip_label = nullptr;
    lv_obj_t *m_wifi_spinner = nullptr;
    lv_timer_t *m_wifi_status_timer = nullptr;

    // Wallpaper page
    lv_obj_t *m_wallpaper_list = nullptr;
    lv_obj_t *m_wallpaper_status = nullptr;
    std::vector<std::string> m_wallpaper_files;

    // Static for cross-task WiFi status (written by prov task, read by LVGL timer)
    static char s_wifi_status_buf[256];
    static bool s_wifi_status_dirty;
    static bool s_wifi_status_done;
    static void wifi_status_cb(const char *status, bool done);

    // Track dynamically loaded wallpaper for cleanup
    void *m_active_wp_data = nullptr;  // malloc'd pixel buffer
};
