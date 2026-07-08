#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void radar_display_init(void);
void radar_display_push(const char *room, const char *move,
                         float wander, float jitter);

/** Check if screen is currently on. */
bool radar_display_is_screen_on(void);

/** Force screen on (for touch wake-up). */
void radar_display_wake_screen(void);

/** Reset idle timer — call periodically to keep screen on (e.g. xiaozhi mode). */
void radar_display_keep_awake(void);

#ifdef __cplusplus
}
#endif
