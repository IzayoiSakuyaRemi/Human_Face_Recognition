/**
 * @file xiaozhi_relay.h
 * @brief XiaoZhi AI voice relay for S3 — bridges UART Opus frames ↔ MQTT cloud
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize MQTT client and connect to xiaozhi cloud */
bool xiaozhi_relay_start(void);

/** Disconnect MQTT and stop relay */
void xiaozhi_relay_stop(void);

/** Called when an Opus audio frame arrives from P4 via UART (type 0x02) */
void xiaozhi_relay_on_opus_from_p4(const uint8_t *opus_data, uint16_t len);

/** Called when a control message arrives from P4 via UART (type 0x04) */
void xiaozhi_relay_on_ctrl_from_p4(uint8_t cmd, const uint8_t *data, uint16_t len);

/** Called when text JSON arrives from P4 via UART (type 0x01) */
void xiaozhi_relay_on_json_from_p4(const char *json);

/** Returns true if relay is active and connected */
bool xiaozhi_relay_is_active(void);

#ifdef __cplusplus
}
#endif
