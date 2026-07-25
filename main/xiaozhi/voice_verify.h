#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize HTTP POST task and PSRAM buffer. Call once at boot. */
void voice_verify_init(void);

/** Feed 960 int16 PCM samples. Accumulates 50 frames, then HTTP POSTs to 3090. */
void voice_verify_feed_pcm(const int16_t *pcm_data, uint16_t sample_count);

#ifdef __cplusplus
}
#endif
