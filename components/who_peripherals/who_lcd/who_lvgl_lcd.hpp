#pragma once
#include "esp_lcd_types.h"
#include "bsp/esp-bsp.h"
#if !BSP_CONFIG_NO_GRAPHIC_LIB
#include "lvgl.h"

namespace who {
namespace lcd {
class WhoLCD {
public:
    // Set true when LVGL display is initialized externally (e.g. by brookesia Phone UI)
    static bool s_skip_hw_init;
    WhoLCD(const lvgl_port_cfg_t &lvgl_port_cfg = {4, 6144, 0, 500, MALLOC_CAP_INTERNAL, 5})
    {
        if (!s_skip_hw_init) init(lvgl_port_cfg);
    }
    ~WhoLCD() { deinit(); }
    void init(const lvgl_port_cfg_t &lvgl_port_cfg);
    void deinit();

private:
    lv_display_t *m_disp;
};
} // namespace lcd
} // namespace who
#endif
