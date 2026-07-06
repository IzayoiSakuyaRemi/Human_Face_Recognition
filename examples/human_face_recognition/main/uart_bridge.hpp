#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize UART1: GPIO4 TX, GPIO5 RX, 921600 baud, 8N1 */
void uart_bridge_init(void);

/** Send string to S3 via UART1. Non-blocking (queued). Returns bytes queued or 0 if full. */
int uart_bridge_send(const char *data);

/* ── Binary frame API (xiaozhi integration) ── */

/**
 * Send a raw binary frame over UART1.
 * Data is sent as-is with the frame type byte already in data[0].
 * Non-blocking — queues to TX task.
 *
 * @param type   Frame type byte (UART_FRAME_* from uart_frame_protocol.h).
 * @param data   Complete frame bytes (including header).
 * @param len    Total frame length in bytes.
 * @return       Bytes queued, or 0 if queue is full.
 */
int uart_bridge_send_frame(uint8_t type, const uint8_t *data, size_t len);

/** Callback type for incoming binary frames */
typedef void (*uart_frame_callback_t)(uint8_t type, const uint8_t *data, size_t len);

/**
 * Register a callback for incoming binary frames.
 * Multiple callbacks can be registered (max 4).
 * Called from uart_rx_task context — keep callbacks short.
 */
void uart_bridge_on_frame(uart_frame_callback_t callback);

/**
 * Register a callback for incoming JSON text lines.
 * Called from uart_rx_task context.
 */
typedef void (*uart_json_callback_t)(const char *line);
void uart_bridge_on_json(uart_json_callback_t callback);

#ifdef __cplusplus
}
#endif
