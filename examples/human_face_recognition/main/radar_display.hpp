#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void radar_display_init(void);
void radar_display_push(const char *room, const char *move,
                         float wander, float jitter);

/** Check if screen is currently on (for touch wake-up). */
bool radar_display_is_screen_on(void);

/** Force screen on (for touch wake-up). */
void radar_display_wake_screen(void);

#ifdef __cplusplus
}
#endif
