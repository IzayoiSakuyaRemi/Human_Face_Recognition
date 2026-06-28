#include "driver/gpio.h"
#include "who_lcd.hpp"
#include "esp_lcd_panel_ops.h"
#include <string.h>

#if BSP_CONFIG_NO_GRAPHIC_LIB
namespace who {
namespace lcd {
esp_lcd_panel_handle_t WhoLCD::get_lcd_panel_handle()
{
#if CONFIG_IDF_TARGET_ESP32S3
    return m_panel_handle;
#elif CONFIG_IDF_TARGET_ESP32P4
    return m_lcd_handles.panel;
#endif
}

#if CONFIG_IDF_TARGET_ESP32S3
void WhoLCD::init()
{
    const bsp_display_config_t bsp_disp_cfg = {
        .max_transfer_sz = BSP_LCD_H_RES * BSP_LCD_V_RES * (BSP_LCD_BITS_PER_PIXEL / 8),
    };
    ESP_ERROR_CHECK(bsp_display_new(&bsp_disp_cfg, &m_panel_handle, &m_io_handle));
    esp_lcd_panel_disp_on_off(m_panel_handle, true);
    ESP_ERROR_CHECK(bsp_display_backlight_on());
    m_lcd_buffer = heap_caps_malloc(BSP_LCD_H_RES * BSP_LCD_V_RES * (BSP_LCD_BITS_PER_PIXEL / 8),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
}

void WhoLCD::deinit()
{
    // TODO release lcd resources.
    heap_caps_free(m_lcd_buffer);
}

void WhoLCD::draw_bitmap(const void *data, int width, int height, int x_start, int y_start)
{
    // 1. 处理全屏刷新
    if (x_start == 0 && y_start == 0 && width == 1024 && height == 600) {
        // 分配 PSRAM 缓冲区用于 180° 旋转
        uint16_t *rotated = (uint16_t *)heap_caps_malloc(width * height * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (rotated) {
            uint16_t *src = (uint16_t *)data;
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    rotated[(height - 1 - y) * width + (width - 1 - x)] = src[y * width + x];
                }
            }
            // 将旋转后的数据发送到屏幕
            esp_lcd_panel_draw_bitmap(m_lcd_handles.panel, 0, 0, width, height, rotated);
            heap_caps_free(rotated);
            return;
        }
    }

    // 2. 对于非全屏刷新，直接绘制原图（如状态栏更新等）
    esp_lcd_panel_draw_bitmap(m_lcd_handles.panel, x_start, y_start, x_start + width, y_start + height, data);
}
#elif CONFIG_IDF_TARGET_ESP32P4
void WhoLCD::init()
{
    bsp_display_config_t bsp_disp_cfg = {
#if CONFIG_BSP_LCD_TYPE_HDMI
#if CONFIG_BSP_LCD_HDMI_800x600_60HZ
        .hdmi_resolution = BSP_HDMI_RES_800x600,
#elif CONFIG_BSP_LCD_HDMI_1280x720_60HZ
        .hdmi_resolution = BSP_HDMI_RES_1280x720,
#elif CONFIG_BSP_LCD_HDMI_1280x800_60HZ
        .hdmi_resolution = BSP_HDMI_RES_1280x800,
#elif CONFIG_BSP_LCD_HDMI_1920x1080_30HZ
        .hdmi_resolution = BSP_HDMI_RES_1920x1080,
#endif
#else
        .hdmi_resolution = BSP_HDMI_RES_NONE,
#endif
        .dsi_bus = {
            .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
            .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
        }};
    bsp_display_new_with_handles(&bsp_disp_cfg, &m_lcd_handles);
    ESP_ERROR_CHECK(bsp_display_brightness_init());
    ESP_ERROR_CHECK(bsp_display_backlight_on());


    uint8_t madctl_reg_val = 0xC0;
    esp_lcd_panel_io_tx_param(m_lcd_handles.io, 0x36, &madctl_reg_val, 1);
}

void WhoLCD::deinit()
{
    bsp_display_delete();
}

void WhoLCD::draw_bitmap(const void *data, int width, int height, int x_start, int y_start)
{
    esp_lcd_panel_draw_bitmap(m_lcd_handles.panel, x_start, y_start, x_start + width, y_start + height, data);
}
#endif
} // namespace lcd
} // namespace who
#endif