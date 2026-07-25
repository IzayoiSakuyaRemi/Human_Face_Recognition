/**
 * @file csi_liveness.hpp
 * @brief CSI-based liveness detection — distinguishes a breathing human from
 *        photos/videos/3D masks by detecting respiration-induced WiFi signal
 *        modulation in the 0.1–0.5 Hz band.
 *
 * Architecture:
 *   S3 captures CSI at 50 Hz → ADR-018 binary frames → UART → P4
 *   P4 uart_rx_task dispatches 0xC5 frames to this module.
 *   Time-domain variance analysis (no FFT, no new dependencies).
 *
 * Calibration: auto-baselines during confirmed-empty periods (radar reports
 * EMPTY + still for ≥30 seconds). Threshold = baseline_RMS × 3.
 */

#ifndef CSI_LIVENESS_HPP
#define CSI_LIVENESS_HPP

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the CSI liveness detector.
 *        Allocates PSRAM buffers for the rolling amplitude window.
 *        Must be called AFTER the UART bridge is up (receives ADR-018 frames).
 */
void csi_liveness_init(void);

/**
 * @brief Feed a raw ADR-018 binary frame to the liveness detector.
 *        Called from uart_rx_task's frame callback (Core 0, no blocking).
 *
 * @param data  Full ADR-018 frame (20-byte header + I/Q payload).
 * @param len   Frame length in bytes.
 */
void csi_liveness_feed(const uint8_t *data, size_t len);

/**
 * @brief Returns true if a breathing human has been detected within the
 *        last WINDOW_SECONDS. Thread-safe — can be called from any core.
 */
bool csi_liveness_check(void);

/**
 * @brief Force recalibration. Call when user confirms the room is empty
 *        (e.g., from Settings UI or after radar reports EMPTY for 30s).
 */
void csi_liveness_calibrate(void);

/**
 * @brief Notify the liveness detector of current room state.
 *        Called from radar JSON parser every 2 seconds.
 *        Used for auto-calibration: when room is EMPTY+still for ≥30s,
 *        the baseline RMS is automatically recaptured.
 */
void csi_liveness_notify_room(const char *room, const char *move);

/**
 * @brief Get the current signal RMS and baseline threshold (for debug/log).
 * @param rms_out        [out] Current RMS of the filtered amplitude signal.
 * @param threshold_out  [out] Current liveness threshold (3× baseline RMS).
 */
void csi_liveness_stats(float *rms_out, float *threshold_out);

#ifdef __cplusplus
}
#endif

#endif /* CSI_LIVENESS_HPP */
