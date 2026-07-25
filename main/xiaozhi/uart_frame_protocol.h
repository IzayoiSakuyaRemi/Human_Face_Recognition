/**
 * @file uart_frame_protocol.h
 * @brief Shared UART multiplexing frame protocol for P4↔S3 communication.
 *
 * Coexists with existing ADR-018 radar binary (0xC5 magic) and JSON text (0x01).
 *
 * Frame types:
 *   0x01 = JSON text ('\n' terminated, compatible with existing format)
 *   0x02 = PCM audio P4→S3 (microphone → cloud)
 *   0x03 = PCM audio S3→P4 (cloud → speaker)
 *   0x04 = Control message (mode switch, radar training, wake word, etc.)
 *   0xC5 = ADR-018 radar binary (existing, unchanged)
 */

#ifndef UART_FRAME_PROTOCOL_H
#define UART_FRAME_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Frame type constants ──────────────────── */
#define UART_FRAME_JSON     0x01  /* JSON text, '\n' terminated */
#define UART_FRAME_PCM_UP   0x02  /* PCM P4→S3 (mic to cloud) */
#define UART_FRAME_PCM_DOWN 0x03  /* PCM S3→P4 (cloud to speaker) */
#define UART_FRAME_CTRL     0x04  /* Control message */
#define UART_FRAME_ADR018   0xC5  /* ADR-018 radar binary (existing) */

/* ── Control command IDs ───────────────────── */
#define CTRL_ENTER_XIAOZHI      0x01  /* P4→S3: enter xiaozhi mode */
#define CTRL_EXIT_XIAOZHI       0x02  /* P4→S3: exit xiaozhi (guard mode) */
#define CTRL_XIAOZHI_READY      0x03  /* S3→P4: xiaozhi ready ACK */
#define CTRL_GUARD_READY        0x04  /* S3→P4: guard mode ready ACK */
#define CTRL_WAKE_WORD          0x05  /* P4→S3: wake word detected */
#define CTRL_VOLUME             0x06  /* P4→S3: volume change */
#define CTRL_RADAR_TRAIN_START  0x07  /* P4→S3: start radar training */
#define CTRL_RADAR_TRAIN_STOP   0x08  /* P4→S3: stop radar training */
#define CTRL_RADAR_TRAIN_CLEAR  0x09  /* P4→S3: clear training data */
#define CTRL_RADAR_STATUS       0x0A  /* S3→P4: training status update */
#define CTRL_RADAR_TRAIN_DONE   0x0B  /* S3→P4: training done + thresholds */
#define CTRL_VOICE_SCORE        0x0E  /* S3→P4: voice verify score JSON */
#define CTRL_VOICE_SRV_ENTER     0x0F  /* P4→S3: enter voice server mode (stop radar) */
#define CTRL_VOICE_SRV_EXIT      0x10  /* P4→S3: exit voice server mode (start radar) */

/* ── PCM frame flags ───────────────────────── */
#define PCM_FLAG_FLUSH      (1 << 0)  /* Flush playback queue */
#define PCM_FLAG_FIRST      (1 << 1)  /* First packet of utterance */

/* ── PCM frame header (6 bytes) ────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  type;          /* 0x02 or 0x03 */
    uint8_t  flags;         /* PCM_FLAG_* bits */
    uint16_t seq;           /* Sequence number (LE) */
    uint16_t sample_count;  /* Number of int16 samples (LE, typically 960) */
} pcm_frame_header_t;

#define PCM_FRAME_HEADER_SIZE  sizeof(pcm_frame_header_t)  /* 6 */
#define PCM_FRAME_MAX_PAYLOAD  1920  /* 960 samples × 2 bytes */
#define PCM_FRAME_CRC_SIZE     2
#define PCM_FRAME_MAX_TOTAL   (PCM_FRAME_HEADER_SIZE + PCM_FRAME_MAX_PAYLOAD + PCM_FRAME_CRC_SIZE)  /* 1928 */

/* ── Control frame header (4 bytes) ────────── */
typedef struct __attribute__((packed)) {
    uint8_t  type;          /* 0x04 */
    uint8_t  cmd;           /* CTRL_* command */
    uint16_t payload_len;   /* JSON payload length (LE), 0 = no payload */
} ctrl_frame_header_t;

#define CTRL_FRAME_HEADER_SIZE  sizeof(ctrl_frame_header_t)  /* 4 */
#define CTRL_FRAME_MAX_JSON     512
#define CTRL_FRAME_MAX_TOTAL   (CTRL_FRAME_HEADER_SIZE + CTRL_FRAME_MAX_JSON)

/* ── API ────────────────────────────────────── */

/**
 * Build a PCM audio frame: header (6B) + raw PCM data + CRC16 (2B).
 *
 * @param buf        Output buffer.
 * @param buf_size   Output buffer size (must be >= PCM_FRAME_MAX_TOTAL).
 * @param type       UART_FRAME_PCM_UP or UART_FRAME_PCM_DOWN.
 * @param flags      PCM_FLAG_* bits.
 * @param seq        Sequence number.
 * @param pcm        int16 PCM samples.
 * @param count      Number of samples (typically 960 for 60ms@16kHz).
 * @return           Total bytes written, or 0 if buffer too small.
 */
size_t uart_frame_build_pcm(uint8_t *buf, size_t buf_size,
                            uint8_t type, uint8_t flags, uint16_t seq,
                            const int16_t *pcm, uint16_t count);

/**
 * Parse a PCM audio frame from received bytes.
 * Validates CRC, extracts header and payload pointer.
 *
 * @param data       Received data buffer.
 * @param len        Number of bytes received.
 * @param hdr        [out] Parsed header.
 * @param pcm_out    [out] Pointer to PCM data inside data buffer (no copy).
 * @param count_out  [out] Number of int16 samples.
 * @return           true if valid frame with good CRC.
 */
bool uart_frame_parse_pcm(const uint8_t *data, size_t len,
                          pcm_frame_header_t *hdr,
                          const int16_t **pcm_out, uint16_t *count_out);

/**
 * Build a control frame: header (4B) + optional JSON payload.
 *
 * @param buf        Output buffer.
 * @param buf_size   Output buffer size.
 * @param cmd        CTRL_* command ID.
 * @param json       JSON string (can be NULL if no payload).
 * @param json_len   Length of JSON string (0 if no payload).
 * @return           Total bytes written, or 0 if buffer too small.
 */
size_t uart_frame_build_ctrl(uint8_t *buf, size_t buf_size,
                             uint8_t cmd, const char *json, uint16_t json_len);

/**
 * CRC16/XMODEM over a byte range.
 */
uint16_t uart_frame_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* UART_FRAME_PROTOCOL_H */
