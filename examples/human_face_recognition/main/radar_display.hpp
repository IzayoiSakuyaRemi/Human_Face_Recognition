#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize radar display: creates LVGL timer to poll radar data from UART.
 * Shows radar status on camera overlay (set_status_text) when camera app is active.
 */
void radar_display_init(void);

/**
 * Called by uart_bridge RX task to push parsed S3 radar data.
 * Thread-safe (FreeRTOS queue).
 */
void radar_display_push(const char *room, const char *move,
                         float wander, float jitter);

#ifdef __cplusplus
}
#endif
