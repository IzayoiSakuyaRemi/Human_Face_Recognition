/**
 * @file event_reporter.h
 * @brief HTTP event reporter for S3 radar (mirrors P4's event_reporter.cpp in C)
 */

#ifndef EVENT_REPORTER_H
#define EVENT_REPORTER_H

#include <stdbool.h>

/**
 * Initialize the event reporter.
 * Creates a FreeRTOS queue and worker task for async HTTP POST.
 */
void event_reporter_init(void);

/**
 * Report a radar status change via HTTP POST to the Flask server.
 * Non-blocking — enqueues the message and returns immediately.
 *
 * @param room    true = OCCUPIED, false = EMPTY
 * @param moving  true = MOVING, false = still
 * @param wander  waveform_wander value
 * @param jitter  waveform_jitter value
 */
void report_radar_event(bool room, bool moving, float wander, float jitter);

#endif
