/**
 * @file uart_audio_codec.h
 * @brief AudioCodec subclass using UART frames instead of I2S.
 *
 * Read()  — dequeues PCM from ring buffer (fed by UART demux task via FeedPcmUp())
 * Write() — builds PCM_DOWN frame + CRC, sends via uart_write_bytes()
 *
 * Parameters: 16kHz, mono, duplex=true
 * Ring buffer: 9600 samples = 600ms buffer depth
 */
#ifndef UART_AUDIO_CODEC_H
#define UART_AUDIO_CODEC_H

#include "audio/audio_codec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class UartAudioCodec : public AudioCodec {
public:
    UartAudioCodec();
    ~UartAudioCodec() override;

    /** Called from UART demux task context when a PCM_UP frame arrives. */
    void FeedPcmUp(const int16_t *data, int samples);

    /** Flush ring buffer — discard all accumulated PCM data. */
    void FlushRingBuf();

    void Start() override;

    // UART codec has no hardware power management — always keep I/O enabled
    void EnableInput(bool enable) override { input_enabled_ = true; }
    void EnableOutput(bool enable) override { output_enabled_ = true; }

protected:
    int Read(int16_t *dest, int samples) override;
    int Write(const int16_t *data, int samples) override;

private:
    static constexpr int kRingBufSize = 9600;  // 600ms @ 16kHz
    int16_t ring_buf_[kRingBufSize];
    volatile int write_pos_ = 0;
    volatile int read_pos_  = 0;
    volatile int available_ = 0;
    uint16_t tx_seq_ = 0;
    SemaphoreHandle_t data_sem_ = nullptr;
    SemaphoreHandle_t mutex_     = nullptr;
};

/** Global pointer for access from C demux task. */
extern UartAudioCodec *g_uart_audio_codec;

#endif /* UART_AUDIO_CODEC_H */
