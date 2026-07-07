/**
 * @file xiaozhi_audio_bridge.hpp
 * @brief P4-side audio bridge for xiaozhi voice relay via S3.
 *
 * During xiaozhi mode:
 *   - mic_task reads 960 samples (60ms@16kHz) from g_mic_handle
 *   - Builds PCM_UP frames → uart_bridge_send_frame() → S3
 *
 *   - uart_bridge_on_frame callback handles PCM_DOWN frames
 *   - PCM_DOWN → esp_codec_dev_write(g_speaker_handle) → speaker
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Start xiaozhi audio bridge: creates mic capture task, registers PCM_DOWN callback. */
void xiaozhi_audio_bridge_start(void);

/** Stop xiaozhi audio bridge: signals mic task to exit, unregisters callback. */
void xiaozhi_audio_bridge_stop(void);

/** Returns true if audio bridge is active. */
bool xiaozhi_audio_bridge_is_active(void);

#ifdef __cplusplus
}
#endif
