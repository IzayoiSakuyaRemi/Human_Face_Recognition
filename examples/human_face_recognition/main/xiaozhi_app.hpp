/**
 * @file xiaozhi_app.hpp
 * @brief XiaoZhi voice assistant app — full-screen brookesia phone app.
 *
 * Displays xiaozhi cloud responses (TTS text, STT text, emotions) on the
 * LVGL display.  Audio bridge is started on run() and stopped on close().
 */

#pragma once

#include "esp_brookesia.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class XiaoZhiApp : public ESP_Brookesia_PhoneApp {
public:
    XiaoZhiApp();
    ~XiaoZhiApp();

    bool run() override;
    bool back() override;
    bool close() override;

    /** Called from UART JSON callback (Core 0).  Thread-safe: pushes to queue. */
    void on_xiaozhi_json(const char *json_line);

private:
    /** LVGL timer callback — drains message queue and updates UI (Core 1). */
    static void timer_cb(lv_timer_t *timer);
    void process_messages();

    lv_obj_t *m_title_label   = nullptr;
    lv_obj_t *m_emoji_img     = nullptr;   // emoji image
    lv_obj_t *m_tts_label     = nullptr;   // xiaozhi reply (TTS)
    lv_obj_t *m_emotion_label = nullptr;   // emotion text
    lv_obj_t *m_status_label  = nullptr;   // connection status
    lv_obj_t *m_exit_btn      = nullptr;
    lv_timer_t *m_timer       = nullptr;

    QueueHandle_t m_msg_queue = nullptr;
    bool m_callback_registered = false;
};
