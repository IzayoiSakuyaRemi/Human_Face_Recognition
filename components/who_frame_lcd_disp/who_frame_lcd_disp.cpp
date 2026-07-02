#include "who_frame_lcd_disp.hpp"

using namespace who::lcd;

namespace who {
namespace lcd_disp {
WhoFrameLCDDisp::WhoFrameLCDDisp(const std::string &name, frame_cap::WhoFrameCapNode *frame_cap_node, int peek_index) :
    task::WhoTask(name), m_lcd(new lcd::WhoLCD()), m_frame_cap_node(frame_cap_node), m_peek_index(peek_index)
{
    frame_cap_node->add_new_frame_signal_subscriber(this);
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    bsp_display_lock(0);
    m_canvas = lv_canvas_create(lv_scr_act());
    lv_obj_set_size(m_canvas, frame_cap_node->get_fb_width(), frame_cap_node->get_fb_height());
    bsp_display_unlock();
#endif
}

WhoFrameLCDDisp::WhoFrameLCDDisp(const std::string &name, frame_cap::WhoFrameCapNode *frame_cap_node, int peek_index, lv_obj_t *parent) :
    task::WhoTask(name), m_lcd(new lcd::WhoLCD()), m_frame_cap_node(frame_cap_node), m_peek_index(peek_index)
{
    frame_cap_node->add_new_frame_signal_subscriber(this);
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    if (parent) {
        bsp_display_lock(0);
        m_canvas = lv_canvas_create(parent);
        lv_obj_set_size(m_canvas, frame_cap_node->get_fb_width(), frame_cap_node->get_fb_height());
        bsp_display_unlock();
    } else {
        m_canvas = nullptr;  // deferred — create_canvas() must be called later
    }
#endif
}

WhoFrameLCDDisp::~WhoFrameLCDDisp()
{
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    if (m_canvas) {
        bsp_display_lock(0);
        lv_obj_del(m_canvas);
        bsp_display_unlock();
    }
#endif
    delete m_lcd;
}

void WhoFrameLCDDisp::set_lcd_disp_cb(const std::function<void(who::cam::cam_fb_t *)> &lcd_disp_cb)
{
    m_lcd_disp_cb = lcd_disp_cb;
}

#if !BSP_CONFIG_NO_GRAPHIC_LIB
lv_obj_t *WhoFrameLCDDisp::get_canvas()
{
    return m_canvas;
}

void WhoFrameLCDDisp::create_canvas(lv_obj_t *parent)
{
    if (m_canvas) return;  // already created
    bsp_display_lock(0);
    m_canvas = lv_canvas_create(parent);
    lv_obj_set_size(m_canvas, m_frame_cap_node->get_fb_width(), m_frame_cap_node->get_fb_height());
    bsp_display_unlock();
}

void WhoFrameLCDDisp::reset_canvas()
{
    m_canvas = nullptr;  // LVGL already destroyed it via screen deletion
}
#endif

void WhoFrameLCDDisp::task()
{
    while (true) {
        EventBits_t event_bits =
            xEventGroupWaitBits(m_event_group, NEW_FRAME | TASK_PAUSE | TASK_STOP, pdTRUE, pdFALSE, portMAX_DELAY);
        if (event_bits & TASK_STOP) {
            break;
        } else if (event_bits & TASK_PAUSE) {
            xEventGroupSetBits(m_event_group, TASK_PAUSED);
            EventBits_t pause_event_bits =
                xEventGroupWaitBits(m_event_group, TASK_RESUME | TASK_STOP, pdTRUE, pdFALSE, portMAX_DELAY);
            if (pause_event_bits & TASK_STOP) {
                break;
            } else {
                continue;
            }
        }
        auto fb = m_frame_cap_node->cam_fb_peek(m_peek_index);
#if BSP_CONFIG_NO_GRAPHIC_LIB
        if (m_lcd_disp_cb) {
            m_lcd_disp_cb(fb);
        }
        m_lcd->draw_bitmap(fb->buf, (int)fb->width, (int)fb->height, 0, 0);
#else
        if (m_canvas) {
            bsp_display_lock(0);
            lv_canvas_set_buffer(m_canvas, fb->buf, fb->width, fb->height, LV_COLOR_FORMAT_NATIVE);
            lv_obj_invalidate(m_canvas);
            if (m_lcd_disp_cb) {
                m_lcd_disp_cb(fb);
            }
            bsp_display_unlock();
        }
#endif
    }
    xEventGroupSetBits(m_event_group, TASK_STOPPED);
    vTaskDelete(NULL);
}
} // namespace lcd_disp
} // namespace who
