#include "who_recognition_app_lcd.hpp"
#include "human_face_detect.hpp"
#include "who_lvgl_utils.hpp"
#include "who_yield2idle.hpp"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG_FACE = "face";

extern EventGroupHandle_t g_recog_event_group;
extern portMUX_TYPE g_face_spinlock;  // global, app_main.cpp
char g_last_recog_face[64] = "Unknown";

LV_FONT_DECLARE(montserrat_bold_26);
LV_FONT_DECLARE(montserrat_bold_20);

namespace who {
namespace app {
WhoRecognitionAppLCD::WhoRecognitionAppLCD(frame_cap::WhoFrameCap *frame_cap) :
    WhoRecognitionAppBase(frame_cap),
    m_lcd_disp(nullptr),
    m_recognition_button(nullptr),
    m_text_result_lcd_disp(nullptr),
    m_detect_result_lcd_disp(nullptr),
    m_label(nullptr),
    m_status_label(nullptr),
    m_exec_label(nullptr)
{
    // Pipeline setup — models, tasks, callbacks.
    // LCD display task is registered here (nullptr parent = canvas deferred to create_ui())
    m_lcd_disp = new lcd_disp::WhoFrameLCDDisp("LCDDisp", frame_cap->get_last_node(), 1, nullptr);
    WhoApp::add_task(m_lcd_disp);
    m_lcd_disp->set_lcd_disp_cb(std::bind(&WhoRecognitionAppLCD::lcd_disp_cb, this, std::placeholders::_1));

    char db_path[64];
#if CONFIG_DB_FATFS_FLASH
    snprintf(db_path, sizeof(db_path), "%s/face.db", CONFIG_SPIFLASH_MOUNT_POINT);
#elif CONFIG_DB_SPIFFS
    snprintf(db_path, sizeof(db_path), "%s/face.db", CONFIG_BSP_SPIFFS_MOUNT_POINT);
#else
    snprintf(db_path, sizeof(db_path), "%s/face.db", CONFIG_BSP_SD_MOUNT_POINT);
#endif
    m_recognition->set_recognizer(new HumanFaceRecognizer(
        db_path, static_cast<HumanFaceFeat::model_type_t>(CONFIG_DEFAULT_HUMAN_FACE_FEAT_MODEL), false));
    m_recognition->set_detect_model(
        new HumanFaceDetect(static_cast<HumanFaceDetect::model_type_t>(CONFIG_DEFAULT_HUMAN_FACE_DETECT_MODEL), false));

    auto recognition_task = m_recognition->get_recognition_task();
    auto detect_task = m_recognition->get_detect_task();
    recognition_task->set_recognition_result_cb(
        std::bind(&WhoRecognitionAppLCD::recognition_result_cb, this, std::placeholders::_1));
    g_recog_event_group = recognition_task->get_event_group();

    recognition_task->set_detect_result_cb(
        std::bind(&WhoRecognitionAppLCD::detect_result_cb, this, std::placeholders::_1));
    recognition_task->set_cleanup_func(std::bind(&WhoRecognitionAppLCD::recognition_cleanup, this));
    detect_task->set_detect_result_cb(std::bind(&WhoRecognitionAppLCD::detect_result_cb, this, std::placeholders::_1));
    detect_task->set_cleanup_func(std::bind(&WhoRecognitionAppLCD::detect_cleanup, this));
}

void WhoRecognitionAppLCD::create_ui(lv_obj_t *parent)
{
    if (m_label) return;  // already created

    // 1. Create canvas on the app's screen (lcd_disp task was registered in constructor)
    if (m_lcd_disp) {
        m_lcd_disp->create_canvas(parent);
    }

    // 2. Labels — children of parent screen
    bsp_display_lock(0);
    m_label = create_lvgl_label("", &montserrat_bold_26, {255, 0, 0}, parent);
    lv_obj_align(m_label, LV_ALIGN_LEFT_MID, 10, 0);

    m_status_label = create_lvgl_label("Cmd: Open Door", &montserrat_bold_20, {255, 255, 255}, parent);
    lv_obj_align(m_status_label, LV_ALIGN_BOTTOM_LEFT, 10, -10);

    m_exec_label = create_lvgl_label("", &montserrat_bold_20, {0, 255, 0}, parent);
    lv_obj_align(m_exec_label, LV_ALIGN_BOTTOM_LEFT, 10, -35);
    bsp_display_unlock();

    // 3. Recognition button (LVGL buttons) on parent
    auto recognition_task = m_recognition->get_recognition_task();
    auto detect_task = m_recognition->get_detect_task();
#if defined(BSP_BOARD_ESP32_S3_EYE) || defined(BSP_BOARD_ESP32_S3_KORVO_2)
    m_recognition_button =
        button::get_recognition_button(button::recognition_button_type_t::PHYSICAL, recognition_task);
#elif defined(BSP_BOARD_ESP32_P4_FUNCTION_EV_BOARD)
    m_recognition_button = button::get_recognition_button(button::recognition_button_type_t::LVGL, recognition_task, parent);
#else
    m_recognition_button =
        button::get_recognition_button(button::recognition_button_type_t::PHYSICAL, recognition_task);
#endif

    // 4. Result display handlers
#if CONFIG_IDF_TARGET_ESP32S3
    int disp_n_frames = 60;
#elif CONFIG_IDF_TARGET_ESP32P4
    int disp_n_frames = 30;
#endif
    m_text_result_lcd_disp = new lcd_disp::WhoTextResultLCDDisp(recognition_task, m_label, disp_n_frames);
    m_detect_result_lcd_disp =
        new lcd_disp::WhoDetectResultLCDDisp(detect_task, m_lcd_disp->get_canvas(), {{255, 0, 0}});
}

void WhoRecognitionAppLCD::reset_ui()
{
    // brookesia's enable_recycle_resource=1 destroys the LVGL screen and all children.
    // Null all pointers so the next create_ui() call recreates them instead of skipping.
    m_label = nullptr;
    m_status_label = nullptr;
    m_exec_label = nullptr;
    if (m_lcd_disp && m_lcd_disp->get_canvas()) {
        // The canvas was destroyed by LVGL screen deletion — prevent dangling pointer
        m_lcd_disp->reset_canvas();
    }
}

WhoRecognitionAppLCD::~WhoRecognitionAppLCD()
{
    delete m_recognition_button;
    delete m_text_result_lcd_disp;
    delete m_detect_result_lcd_disp;
    bsp_display_lock(0);
    if (m_exec_label) lv_obj_del(m_exec_label);
    if (m_status_label) lv_obj_del(m_status_label);
    if (m_label) lv_obj_del(m_label);
    bsp_display_unlock();
}

bool WhoRecognitionAppLCD::run()
{
    bool ret = WhoYield2Idle::get_instance()->run();
    for (const auto &frame_cap_node : m_frame_cap->get_all_nodes()) {
        // VIDIOC_DQBUF ioctl call chain (VFS→V4L2→MIPI-CSI→ISP) needs 6-8KB
        ret &= frame_cap_node->run(8192, 2, 0);
    }
    if (m_lcd_disp) {
        // lv_canvas_set_buffer + lv_obj_invalidate inside LVGL lock
        ret &= m_lcd_disp->run(4096, 2, 0);
    }
    ret &= m_recognition->get_detect_task()->run(3584, 2, 1);
    ret &= m_recognition->get_recognition_task()->run(3584, 2, 1);
    return ret;
}

void WhoRecognitionAppLCD::recognition_result_cb(const std::string &result)
{
    if (m_text_result_lcd_disp) m_text_result_lcd_disp->save_text_result(result);
    portENTER_CRITICAL(&g_face_spinlock);
    strncpy(g_last_recog_face, result.c_str(), sizeof(g_last_recog_face) - 1);
    portEXIT_CRITICAL(&g_face_spinlock);

    // Suppress noisy "Failed to recognize" warnings — caused by edge-cut
    // faces after switching to ESPDET-PICO (different box output). The
    // detector catches more faces including partial ones at frame edges.
    if (result != "who?")
        ESP_LOGI(TAG_FACE, "Recognition: %s", result.c_str());

    // Report face recognition to server (via UART → S3 → HTTP)
    int id = -1; float sim = 0;
    if (sscanf(result.c_str(), "id: %d, sim: %f", &id, &sim) == 2) {
        char buf[128];
        int len = snprintf(buf, sizeof(buf),
            "{\"device\":\"p4-voice\",\"event\":\"face_recognized\","
            "\"user_id\":%d,\"sim\":%.3f}\n", id, sim);
        if (len > 0 && len < (int)sizeof(buf)) uart_write_bytes(UART_NUM_1, buf, len);
    } else if (result == "who?") {
        const char *msg = "{\"device\":\"p4-voice\",\"event\":\"face_unknown\"}\n";
        uart_write_bytes(UART_NUM_1, msg, strlen(msg));
    }
}

/* ── Last-face cache (for recognition fallback) ───────────────
 * When a voice command triggers RECOGNIZE, the recognition task
 * replaces the detect callback and waits for the NEXT detection.
 * If that frame has no face (timing jitter), recognition fails.
 *
 * This cache stores a deep copy of the most recent face crop.
 * When RECOGNIZE fires and the fresh frame has no face, the
 * cached face image can be fed to the recognizer directly.
 * ─────────────────────────────────────────────────────────── */

void WhoRecognitionAppLCD::detect_result_cb(const detect::WhoDetect::result_t &result)
{
    if (m_detect_result_lcd_disp) m_detect_result_lcd_disp->save_detect_result(result);

    // Throttled detection log: once per second max
    static int64_t last_log_us = 0;
    int64_t now = esp_timer_get_time();
    if (now - last_log_us > 1000000) {
        last_log_us = now;
        int n = (int)result.det_res.size();
        if (n > 0) {
            char buf[256]; int pos = 0;
            int count = 0;
            for (auto &d : result.det_res) {
                if (count >= 4) break;
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "%s[%d,%d %dx%d s=%.2f]",
                    count ? " " : "",
                    d.box[0], d.box[1], d.box[2]-d.box[0], d.box[3]-d.box[1],
                    (double)d.score);
                count++;
            }
            ESP_LOGI(TAG_FACE, "Detect: %d face(s) %s", n, buf);
        }
    }
}

void WhoRecognitionAppLCD::lcd_disp_cb(who::cam::cam_fb_t *fb)
{
    if (m_detect_result_lcd_disp) m_detect_result_lcd_disp->lcd_disp_cb(fb);
    if (m_text_result_lcd_disp) m_text_result_lcd_disp->lcd_disp_cb(fb);
}

void app::WhoRecognitionAppLCD::recognition_cleanup()
{
    if (m_text_result_lcd_disp) m_text_result_lcd_disp->cleanup();
}

void app::WhoRecognitionAppLCD::detect_cleanup()
{
    if (m_detect_result_lcd_disp) m_detect_result_lcd_disp->cleanup();
}

void WhoRecognitionAppLCD::set_status_text(const char *text)
{
    bsp_display_lock(0);
    if (m_status_label) lv_label_set_text(m_status_label, text);
    bsp_display_unlock();
}

void WhoRecognitionAppLCD::set_exec_text(const char *text)
{
    bsp_display_lock(0);
    if (m_exec_label) {
        lv_label_set_text(m_exec_label, text);
        // Red for Alarm, green for Allow
        lv_color_t c = (strstr(text, "Alarm")) ? lv_color_make(255, 0, 0) : lv_color_make(0, 255, 0);
        lv_obj_set_style_text_color(m_exec_label, c, LV_PART_MAIN);
    }
    bsp_display_unlock();
}

} // namespace app
} // namespace who
