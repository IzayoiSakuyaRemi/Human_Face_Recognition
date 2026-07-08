/**
 * @file xiaozhi_emoji.hpp
 * @brief Emotion-to-emoji image mapping for xiaozhi voice assistant.
 *
 * Preloads 4 emoji .rgb565 images from SD card at init time.
 * Switching between emotions is a zero-I/O pointer swap via lv_image_set_src().
 * All LVGL operations happen on Core 1 (LVGL task / timer).
 */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Preload 4 emoji images from /sdcard/xz/ into PSRAM.
 *  Must be called from LVGL task context (Core 1).
 *  @return true if at least one image loaded. */
bool xz_emoji_init(void);

/** Free all preloaded emoji memory. */
void xz_emoji_deinit(void);

/** Map an emotion string (lowercase) to a preloaded lv_image_dsc_t.
 *  Always returns a valid pointer (falls back to neutral).
 *  Thread-safe: only reads static data populated during init. */
const lv_image_dsc_t *xz_emoji_get(const char *emotion_str);

#ifdef __cplusplus
}
#endif
