/**
 * @file xiaozhi_emoji.cpp
 * @brief Preload 4 emoji .rgb565 images from SD card into PSRAM.
 *
 * File format: 8-byte LE header (uint32 w, uint32 h) + raw RGB565 pixels.
 * All images preloaded at init time.  Emotion switching is a pointer swap
 * via lv_image_set_src() — zero SD card I/O at runtime.
 */

#include "xiaozhi_emoji.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "xz_emoji";

/* ── 4 emoji slots ─────────────────────────── */
#define EMOJI_COUNT 4

enum {
    SLOT_POSITIVE = 0,   // happy, funny, confident
    SLOT_ANGRY    = 1,   // angry
    SLOT_SURPRISE = 2,   // shocked, embarrassed
    SLOT_NEUTRAL  = 3,   // neutral, sleepy, sad (sad→neutral beats sad→angry)
};

static const char *EMOJI_FILES[EMOJI_COUNT] = {
    "/sdcard/xz/silly.rgb565",
    "/sdcard/xz/angry.rgb565",
    "/sdcard/xz/shocked1.rgb565",
    "/sdcard/xz/neutral.rgb565",
};

static lv_image_dsc_t *s_dsc[EMOJI_COUNT]   = {};
static void          *s_pixels[EMOJI_COUNT] = {};

/* ── Load one .rgb565 emoji ────────────────── */
static bool load_one(int slot)
{
    FILE *f = fopen(EMOJI_FILES[slot], "rb");
    if (!f) {
        ESP_LOGW(TAG, "Cannot open %s", EMOJI_FILES[slot]);
        return false;
    }

    uint8_t hdr[8];
    if (fread(hdr, 1, 8, f) != 8) {
        ESP_LOGE(TAG, "Short header: %s", EMOJI_FILES[slot]);
        fclose(f); return false;
    }

    uint32_t w = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8)
               | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    uint32_t h = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8)
               | ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);

    if (w == 0 || h == 0 || w > 512 || h > 1024) {
        ESP_LOGE(TAG, "Bad dims %" PRIu32 "x%" PRIu32 " in %s", w, h, EMOJI_FILES[slot]);
        fclose(f); return false;
    }

    uint32_t data_size = w * h * 2;
    void *pixels = heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM);
    if (!pixels) {
        ESP_LOGE(TAG, "OOM: %" PRIu32 " bytes for %s", data_size, EMOJI_FILES[slot]);
        fclose(f); return false;
    }

    size_t rd = fread(pixels, 1, data_size, f);
    fclose(f);
    if (rd != data_size) {
        ESP_LOGE(TAG, "Short read: %zu/%" PRIu32, rd, data_size);
        heap_caps_free(pixels); return false;
    }

    lv_image_dsc_t *dsc = (lv_image_dsc_t *)calloc(1, sizeof(lv_image_dsc_t));
    if (!dsc) { heap_caps_free(pixels); return false; }

    dsc->header.magic  = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf     = LV_COLOR_FORMAT_RGB565;
    dsc->header.w      = (uint16_t)w;
    dsc->header.h      = (uint16_t)h;
    dsc->header.stride = (uint16_t)(w * 2);
    dsc->data_size     = data_size;
    dsc->data          = (const uint8_t *)pixels;

    s_dsc[slot]   = dsc;
    s_pixels[slot] = pixels;

    ESP_LOGI(TAG, "Loaded %s: %" PRIu32 "x%" PRIu32 " (%" PRIu32 " bytes)",
             EMOJI_FILES[slot], w, h, data_size);
    return true;
}

/* ── Public API ────────────────────────────── */

bool xz_emoji_init(void)
{
    int ok = 0;
    for (int i = 0; i < EMOJI_COUNT; i++)
        if (load_one(i)) ok++;
    ESP_LOGI(TAG, "Emoji init: %d/%d loaded", ok, EMOJI_COUNT);
    return ok > 0;
}

void xz_emoji_deinit(void)
{
    for (int i = 0; i < EMOJI_COUNT; i++) {
        if (s_dsc[i])    { free(s_dsc[i]);    s_dsc[i]    = nullptr; }
        if (s_pixels[i]) { heap_caps_free(s_pixels[i]); s_pixels[i] = nullptr; }
    }
}

const lv_image_dsc_t *xz_emoji_get(const char *emotion_str)
{
    if (!emotion_str) return s_dsc[SLOT_NEUTRAL];

    int slot;

    // Positive: happy, funny, confident, laughing, cool, loving, winking
    if (strcmp(emotion_str, "happy") == 0 || strcmp(emotion_str, "funny") == 0
     || strcmp(emotion_str, "confident") == 0)
        slot = SLOT_POSITIVE;

    // Angry
    else if (strcmp(emotion_str, "angry") == 0)
        slot = SLOT_ANGRY;

    // Surprise: shocked, embarrassed, surprised
    else if (strcmp(emotion_str, "shocked") == 0
          || strcmp(emotion_str, "embarrassed") == 0
          || strcmp(emotion_str, "surprised") == 0)
        slot = SLOT_SURPRISE;

    // Neutral baseline: neutral, sleepy, sad (sad→neutral beats sad→angry)
    else if (strcmp(emotion_str, "neutral") == 0
          || strcmp(emotion_str, "sleepy") == 0
          || strcmp(emotion_str, "sad") == 0)
        slot = SLOT_NEUTRAL;

    else
        slot = SLOT_NEUTRAL;  // unknown → neutral

    // Fallback: if slot failed to load, use neutral
    if (!s_dsc[slot]) slot = SLOT_NEUTRAL;
    if (!s_dsc[slot]) return nullptr;

    return s_dsc[slot];
}
