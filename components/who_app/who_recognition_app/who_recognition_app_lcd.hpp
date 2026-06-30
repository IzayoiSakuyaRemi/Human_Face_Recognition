#pragma once
#include "who_detect_result_handle.hpp"
#include "who_frame_lcd_disp.hpp"
#include "who_recognition_app_base.hpp"
#include "who_text_result_handle.hpp"

namespace who {
namespace app {
class WhoRecognitionAppLCD : public WhoRecognitionAppBase {
public:
    WhoRecognitionAppLCD(frame_cap::WhoFrameCap *frame_cap);
    ~WhoRecognitionAppLCD();
    bool run() override;

protected:
    virtual void recognition_result_cb(const std::string &result);
    virtual void detect_result_cb(const detect::WhoDetect::result_t &result);
    virtual void lcd_disp_cb(who::cam::cam_fb_t *fb);
    virtual void recognition_cleanup();
    virtual void detect_cleanup();

private:
    lcd_disp::WhoFrameLCDDisp *m_lcd_disp;
    button::WhoRecognitionButton *m_recognition_button;
    lcd_disp::WhoTextResultLCDDisp *m_text_result_lcd_disp;
    lcd_disp::WhoDetectResultLCDDisp *m_detect_result_lcd_disp;
    lv_obj_t *m_label;
    lv_obj_t *m_status_label;
    lv_obj_t *m_exec_label;
    lv_obj_t *m_wifi_label;

public:
    void set_status_text(const char *text);
    void set_exec_text(const char *text);
    void set_wifi_text(const char *text);
    static void wifi_btn_click_cb(lv_event_t *e);
};
} // namespace app
} // namespace who

// Global: set before creating WhoRecognitionAppLCD to handle WiFi button clicks
extern void (*g_on_wifi_btn_click)();
