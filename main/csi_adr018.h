/**
 * @file csi_adr018.h
 * @brief ADR-018 binary CSI frame serialization header.
 */

#ifndef CSI_ADR018_H
#define CSI_ADR018_H

#include <stdint.h>
#include <stddef.h>
#include "esp_wifi_types.h"

/**
 * Serialize CSI data into ADR-018 binary frame format.
 *
 * Layout (matching RuView csi_collector.c exactly):
 *   [0..3]   Magic: 0xC5110001 (LE)
 *   [4]      Node ID
 *   [5]      Number of antennas (always 1 for ESP32-S3)
 *   [6..7]   Number of subcarriers (LE u16)
 *   [8..11]  Frequency MHz (LE u32)
 *   [12..15] Sequence number (LE u32, auto-incremented)
 *   [16]     RSSI (i8)
 *   [17]     Noise floor (i8)
 *   [18]     PPDU type (0=legacy/HT, 0xFF=unknown)
 *   [19]     Reserved
 *   [20..]   Raw I/Q bytes from ESP-IDF CSI callback
 *
 * @param info     WiFi CSI info from the ESP-IDF callback.
 * @param node_id  Node identifier (0-255).
 * @param buf      Output buffer.
 * @param buf_len  Size of output buffer.
 * @return Number of bytes written, or 0 on error.
 */
size_t csi_serialize_adr018(const wifi_csi_info_t *info, uint8_t node_id,
                             uint8_t *buf, size_t buf_len);

#endif /* CSI_ADR018_H */
