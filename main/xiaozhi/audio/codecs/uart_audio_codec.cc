/**
 * @file uart_audio_codec.cc
 * @brief UartAudioCodec implementation.
 */

#include "uart_audio_codec.h"
#include "xiaozhi/uart_frame_protocol.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "uart_codec";

UartAudioCodec *g_uart_audio_codec = nullptr;

/* ── C-linkage wrapper for C demux task ──────── */
extern "C" void uart_audio_codec_feed_pcm(const int16_t *data, int samples) {
    if (g_uart_audio_codec)
        g_uart_audio_codec->FeedPcmUp(data, samples);
}

UartAudioCodec::UartAudioCodec() {
    duplex_             = true;
    input_sample_rate_  = 16000;
    output_sample_rate_ = 16000;
    input_channels_     = 1;
    output_channels_    = 1;
    input_enabled_      = false;
    output_enabled_     = false;

    data_sem_ = xSemaphoreCreateBinary();
    mutex_    = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "Created: 16kHz mono, duplex, %d-sample ring buffer", kRingBufSize);
}

UartAudioCodec::~UartAudioCodec() {
    if (data_sem_) { vSemaphoreDelete(data_sem_); data_sem_ = nullptr; }
    if (mutex_)    { vSemaphoreDelete(mutex_);    mutex_    = nullptr; }
    if (g_uart_audio_codec == this) g_uart_audio_codec = nullptr;
}

void UartAudioCodec::Start() {
    input_enabled_  = true;
    output_enabled_ = true;
    ESP_LOGI(TAG, "Started");
}

/* ── FlushRingBuf: discard all buffered PCM ───── */

void UartAudioCodec::FlushRingBuf() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    write_pos_ = 0;
    read_pos_  = 0;
    available_ = 0;
    xSemaphoreGive(mutex_);
    ESP_LOGI(TAG, "Ring buffer flushed");
}

/* ── FeedPcmUp: called from UART demux task ───── */

void UartAudioCodec::FeedPcmUp(const int16_t *data, int samples) {
    if (!data || samples <= 0) return;

    if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        return;  // should never happen with portMAX_DELAY
    }

    int free_space = kRingBufSize - available_;
    if (samples > free_space) {
        // Drop oldest data to make room
        int overflow = samples - free_space;
        read_pos_ = (read_pos_ + overflow) % kRingBufSize;
        available_ -= overflow;
        ESP_LOGW(TAG, "Ring buffer overflow, dropped %d samples", overflow);
    }

    // Copy with wrap-around
    int space_to_end = kRingBufSize - write_pos_;
    if (samples <= space_to_end) {
        memcpy(&ring_buf_[write_pos_], data, samples * sizeof(int16_t));
    } else {
        memcpy(&ring_buf_[write_pos_], data, space_to_end * sizeof(int16_t));
        memcpy(&ring_buf_[0], data + space_to_end, (samples - space_to_end) * sizeof(int16_t));
    }
    write_pos_ = (write_pos_ + samples) % kRingBufSize;
    available_ += samples;

    xSemaphoreGive(mutex_);
    xSemaphoreGive(data_sem_);  // Wake up blocked Read()
}

/* ── Read: blocking read for AudioService ─────── */

int UartAudioCodec::Read(int16_t *dest, int samples) {
    if (!dest || samples <= 0) return 0;

    // Block until data is available
    if (xSemaphoreTake(data_sem_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;  // timeout, return silence
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);

    int to_read = (samples < available_) ? samples : available_;
    if (to_read == 0) {
        xSemaphoreGive(mutex_);
        return 0;
    }

    // Copy with wrap-around
    int space_to_end = kRingBufSize - read_pos_;
    if (to_read <= space_to_end) {
        memcpy(dest, &ring_buf_[read_pos_], to_read * sizeof(int16_t));
    } else {
        memcpy(dest, &ring_buf_[read_pos_], space_to_end * sizeof(int16_t));
        memcpy(dest + space_to_end, &ring_buf_[0], (to_read - space_to_end) * sizeof(int16_t));
    }
    read_pos_ = (read_pos_ + to_read) % kRingBufSize;
    available_ -= to_read;

    // If more data remains, re-signal
    if (available_ > 0) {
        xSemaphoreGive(data_sem_);
    }

    xSemaphoreGive(mutex_);
    return to_read;
}

/* ── Write: build PCM_DOWN frame + UART TX ────── */

int UartAudioCodec::Write(const int16_t *data, int samples) {
    if (!data || samples <= 0) return 0;

    uint8_t fbuf[PCM_FRAME_MAX_TOTAL];
    size_t flen = uart_frame_build_pcm(
        fbuf, sizeof(fbuf),
        UART_FRAME_PCM_DOWN, 0, tx_seq_++,
        data, (uint16_t)samples);

    if (flen == 0) {
        ESP_LOGE(TAG, "Write: frame build failed for %d samples", samples);
        return 0;
    }

    int sent = uart_write_bytes(UART_NUM_1, (const char *)fbuf, flen);
    if (sent < 0) {
        ESP_LOGE(TAG, "Write: uart_write_bytes failed");
        return 0;
    }
    return samples;
}
