/**
 * @file csi_adr018.c
 * @brief ADR-018 binary CSI frame serialization (RuView compliant).
 *
 * Strictly follows RuView firmware/esp32-csi-node/main/csi_collector.c
 * with the exact same 20-byte header layout and magic number.
 */

#include "csi_adr018.h"
#include <string.h>

/** ADR-018 magic number (0xC5110001, little-endian). */
#define CSI_MAGIC 0xC5110001u

/** ADR-018 header size in bytes. */
#define CSI_HEADER_SIZE 20

/** Maximum frame buffer size (header + 4 antennas * 256 subcarriers * 2 bytes). */
#define CSI_MAX_FRAME_SIZE (CSI_HEADER_SIZE + 4 * 256 * 2)

size_t csi_serialize_adr018(const wifi_csi_info_t *info, uint8_t node_id,
                             uint8_t *buf, size_t buf_len)
{
    if (!info || !buf || !info->buf) return 0;

    uint8_t  n_ant = 1;  /* ESP32-S3 reports 1 antenna for CSI */
    uint16_t iq_len = (uint16_t)info->len;
    uint16_t n_sub  = iq_len / (2 * n_ant);
    size_t   frame  = CSI_HEADER_SIZE + iq_len;
    if (frame > buf_len) return 0;

    /* Frequency from channel number */
    uint8_t  ch  = info->rx_ctrl.channel;
    uint32_t mhz;
    if (ch >= 1 && ch <= 13)       mhz = 2412 + (ch - 1) * 5;
    else if (ch == 14)             mhz = 2484;
    else if (ch >= 36 && ch <= 177) mhz = 5000 + ch * 5;
    else                            mhz = 0;

    /* Magic (LE) */
    uint32_t magic = CSI_MAGIC;
    memcpy(&buf[0], &magic, 4);

    buf[4] = node_id;                    /* Node ID */
    buf[5] = n_ant;                      /* Antenna count */
    memcpy(&buf[6],  &n_sub, 2);         /* Subcarrier count (u16 LE) */
    memcpy(&buf[8],  &mhz,   4);         /* Frequency MHz (u32 LE) */
    {
        /* Sequence number (u32 LE) */
        static uint32_t seq = 0;
        uint32_t s = seq++;
        memcpy(&buf[12], &s, 4);
    }
    buf[16] = (uint8_t)(int8_t)info->rx_ctrl.rssi;        /* RSSI (i8) */
    buf[17] = (uint8_t)(int8_t)info->rx_ctrl.noise_floor; /* Noise floor (i8) */
    buf[18] = 0;  /* PPDU type: 0=legacy/HT, 0xFF=unknown */
    buf[19] = 0;  /* Reserved / flags */

    /* Raw I/Q payload */
    memcpy(&buf[20], info->buf, iq_len);

    return frame;
}
